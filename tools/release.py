"""Publish firmware to GitHub so the CYD can update itself over the internet.

  py -3.12 tools/release.py setup
      One time, after `gh auth login`. Creates a public GitHub repo
      "cyd-dashboard", points the firmware's update URL at it, and publishes v1.
      Then flash the board ONCE over USB:  py -3.12 -m platformio run -t upload

  py -3.12 tools/release.py publish "what changed"
      Bump FW_VERSION in src/version.h first. Builds, commits, pushes, and
      creates a GitHub release with firmware.bin + manifest.json. Boards pick it
      up within 6 hours, or on their next restart.

The repo must be public so the board can download without a password. It holds
only code: Wi-Fi details live on the board, never in the firmware.
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = "cyd-dashboard"
FIRMWARE = ROOT / ".pio" / "build" / "cyd" / "firmware.bin"
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


def build():
    run(sys.executable, "-m", "platformio", "run")
    if not FIRMWARE.exists():
        sys.exit("build produced no firmware.bin")


def release(login, notes):
    v = fw_version()
    if run("gh", "release", "view", f"v{v}", check=False, capture=True).returncode == 0:
        sys.exit(f"Release v{v} already exists - bump FW_VERSION in src/version.h first.")
    build()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    manifest = OUT_DIR / "manifest.json"
    url = f"https://github.com/{login}/{REPO}/releases/download/v{v}/firmware.bin"
    manifest.write_text(json.dumps({"version": v, "url": url}, indent=2) + "\n")
    commit_all(f"Release v{v}: {notes}")
    run("git", "push", "-q", "origin", "main")
    run("gh", "release", "create", f"v{v}", str(FIRMWARE), str(manifest),
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
    url = f"https://github.com/{login}/{REPO}/releases/latest/download/manifest.json"
    cfg.write_text(re.sub(r'#define OTA_MANIFEST_URL ".*"', f'#define OTA_MANIFEST_URL "{url}"', cfg.read_text()))
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
        release(gh_login()[0], sys.argv[2])
    else:
        print(__doc__)
