"""Grab a screenshot from the hub over serial and save it as a PNG.

Usage: python tools/snap.py [PORT] [OUT.png] [--cmd "screen network"]

Sends an optional console command, then `snap`, and decodes the RLE dump
(one line per row of "CCCCNN" tokens: RGB565 color + run length).
Requires pyserial and Pillow.
"""
import argparse
import time

import serial
from PIL import Image


def rgb565(c):
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default="COM22")
    ap.add_argument("out", nargs="?", default="snap.png")
    ap.add_argument("--cmd", action="append", default=[], help="console command to run first")
    ap.add_argument("--wait", type=float, default=1.5, help="seconds to wait after commands")
    args = ap.parse_args()

    # dsrdtr/rtscts off and lines held low so opening the port doesn't reset the board
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 20
    ser.dtr = False
    ser.rts = False
    ser.open()

    # Opening the port can still reset some boards: wait until the log goes quiet.
    ser.timeout = 0.5
    deadline = time.time() + 8
    while ser.read(4096) and time.time() < deadline:
        pass
    ser.timeout = 20

    for cmd in args.cmd:
        ser.write((cmd + "\n").encode())
        time.sleep(0.3)
    if args.cmd:
        time.sleep(args.wait)
    ser.reset_input_buffer()
    ser.write(b"snap\n")

    while True:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            raise SystemExit("timed out waiting for SNAP BEGIN")
        if line.startswith("SNAP BEGIN"):
            _, _, w, h = line.split()
            w, h = int(w), int(h)
            break

    img = Image.new("RGB", (w, h))
    px = img.load()
    y = 0
    while y < h:
        line = ser.readline().decode(errors="replace").strip()
        if not line or line.startswith("["):
            continue  # log lines from other modules can interleave
        x = 0
        for i in range(0, len(line) - 5, 6):
            color = rgb565(int(line[i:i + 4], 16))
            for _ in range(int(line[i + 4:i + 6], 16)):
                if x < w:
                    px[x, y] = color
                x += 1
        y += 1

    img.save(args.out)
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
