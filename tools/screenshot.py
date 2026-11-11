"""Grab a screenshot of the CYD fish tank over USB serial and save it as a PNG.

Usage: py -3.12 tools/screenshot.py [out.png] [season]
  season (optional): a / n / t / c  (auto / none / thanksgiving / christmas)

The board renders its next tank frame, streams the raw RGB565 pixels, and this
script converts them. Takes ~15 s at 115200 baud. No extra Python packages needed.
"""
import struct
import sys
import time
import zlib

import serial
from serial.tools import list_ports

CH340_VID = 0x1A86


def write_png(path, w, h, rgb_rows):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def rgb565_rows(data, w, h):
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            hi, lo = data[(y * w + x) * 2], data[(y * w + x) * 2 + 1]
            v = (hi << 8) | lo  # sprite buffer stores pixels byte-swapped (big-endian)
            r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
            row += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
        rows.append(bytes(row))
    return rows


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "screenshot.png"
    season = sys.argv[2] if len(sys.argv) > 2 else None

    ports = [p.device for p in list_ports.comports() if p.vid == CH340_VID]
    if not ports:
        sys.exit("No CH340 (CYD) serial port found - is the board plugged in?")

    # Set DTR/RTS before opening: asserting them on open would reset the board
    port = serial.Serial(baudrate=115200, timeout=20)
    port.port = ports[0]
    port.dtr = False
    port.rts = False
    port.open()
    with port as s:
        time.sleep(0.3)
        s.reset_input_buffer()
        if season:
            s.write(season.encode())
            time.sleep(1.0)  # let a few frames render with the new season
        s.write(b"f")
        time.sleep(1.5)  # let the tank settle in if we just switched to it
        s.reset_input_buffer()
        s.write(b"s")

        # Wait for the header line
        deadline = time.time() + 20
        while True:
            if time.time() > deadline:
                sys.exit("Timed out waiting for the board to start the screenshot")
            line = s.readline().decode(errors="replace").strip()
            if line.startswith("SHOT ") and line != "SHOT END":
                w, h = map(int, line.split()[1:3])
                break

        need = w * h * 2
        data = bytearray()
        while len(data) < need:
            chunk = s.read(need - len(data))
            if not chunk:
                sys.exit(f"Timed out mid-transfer ({len(data)}/{need} bytes)")
            data += chunk

    write_png(out, w, h, rgb565_rows(data, w, h))
    print(f"saved {out} ({w}x{h}) from {ports[0]}")


if __name__ == "__main__":
    main()
