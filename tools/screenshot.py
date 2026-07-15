#!/usr/bin/env python3
"""Grab a screenshot from a running Musician Dial over its USB serial port.

Requires: pip install pyserial pillow

Usage:
    python3 tools/screenshot.py [output.png] [--port /dev/ttyACM0]

With no output path, saves a timestamped PNG under ~/Pictures/Screenshots/esp32
(created if it doesn't exist).

With no --port, auto-detects the board by its USB VID:PID (303a:1001, the
native ESP32-S3 USB-JTAG/serial controller -- see README's Hardware
section). This matters because ttyACM device numbering isn't stable: it
depends on what else is plugged in and the order things enumerated, not on
which device is actually the board.
"""
import argparse
import base64
import datetime
import os
import re
import sys

import serial
import serial.tools.list_ports
from PIL import Image

BOARD_VID = 0x303A
BOARD_PID = 0x1001
DEFAULT_DIR = os.path.expanduser("~/Pictures/Screenshots/esp32")


def find_port() -> str:
    matches = [p.device for p in serial.tools.list_ports.comports() if p.vid == BOARD_VID and p.pid == BOARD_PID]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        raise RuntimeError(f"multiple boards found ({matches}); pass --port to pick one")
    raise RuntimeError(
        f"no device with VID:PID {BOARD_VID:04x}:{BOARD_PID:04x} found -- is it plugged in? "
        "Pass --port to override."
    )


def capture(port: str) -> tuple[int, int, int, bytes]:
    ser = serial.Serial(port, 115200, timeout=10)
    try:
        ser.reset_input_buffer()
        ser.write(b"SCREENSHOT\n")
        ser.flush()

        header = None
        b64_lines = []
        state = "waiting"

        while True:
            line = ser.readline()
            if not line:
                raise TimeoutError("timed out waiting for the device")
            try:
                text = line.decode("ascii").strip()
            except UnicodeDecodeError:
                continue

            if state == "waiting":
                m = re.match(r"---SCREENSHOT-BEGIN w=(\d+) h=(\d+) stride=(\d+) size=(\d+)---", text)
                if m:
                    header = tuple(int(x) for x in m.groups())
                    state = "reading"
                elif text == "---SCREENSHOT-FAIL---":
                    raise RuntimeError("device reported a snapshot failure")
            elif state == "reading":
                if text == "---SCREENSHOT-END---":
                    break
                b64_lines.append(text)
    finally:
        ser.close()

    w, h, stride, size = header
    raw = base64.b64decode("".join(b64_lines))
    if len(raw) != size:
        print(f"warning: got {len(raw)} bytes, device reported {size}", file=sys.stderr)
    return w, h, stride, raw


def rgb565_to_image(w: int, h: int, stride: int, raw: bytes) -> Image.Image:
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        row_off = y * stride
        for x in range(w):
            off = row_off + x * 2
            v = raw[off] | (raw[off + 1] << 8)
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            px[x, y] = (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    return img


def default_output_path() -> str:
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    return os.path.join(DEFAULT_DIR, f"musician-dial-{stamp}.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default=None,
                        help=f"output path; defaults to a timestamped file under {DEFAULT_DIR}")
    parser.add_argument("--port", default=None, help="serial port; auto-detected by VID:PID if omitted")
    args = parser.parse_args()

    output = args.output or default_output_path()
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)

    port = args.port or find_port()
    w, h, stride, raw = capture(port)
    img = rgb565_to_image(w, h, stride, raw)
    img.save(output)
    print(f"Saved {w}x{h} screenshot to {output} (via {port})")


if __name__ == "__main__":
    main()
