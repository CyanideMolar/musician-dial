# Musician Dial α

ESP-IDF v5 firmware for the Waveshare **ESP32-S3-Touch-LCD-2.1** (480×480 round
capacitive touch display) — a dedicated hardware practice tool for musicians.
Two full-screen apps, swipe up/down to switch between them.

## Apps

**Metronome**
- Pendulum needle or pulsing beat ring (switchable), 90-degree pendulum swing
  with blue tick marks showing the swing endpoints
- Tap tempo, averaging the last 12 taps for accuracy; long-press the BPM
  value for direct numeric entry
- +/- BPM buttons with hold-to-repeat
- Time signature picker (4/4, 3/4, 6/8) via long-press on Start/Stop
- Beat 1 fires immediately on Start — no lag waiting for the first periodic
  timer tick, so it's usable to cue off a song you're already listening to
- Green Start / red Stop button coloring

**Circle of Fifths**
- 12-key color wheel around the rim; tap a key for its major-key info,
  long-press for its parallel minor (not the relative minor — same tonic,
  different key)
- Notes, primary triads (I-IV-V-vi / i-iv-v-III), and the remaining
  non-diminished triads (ii-iii / VI-VII), chord names in bold
- Selection shown as a gold outline behind the key, not a fill, so each
  key's own wheel color stays visible
- Per-key text color (black or white) picked by actual WCAG contrast ratio
  against that key's fill color, not just eyeballed

## Hardware

Everything needed lives on the board — no wiring required. Confirmed pin/bus
map (sourced from Waveshare's own reference firmware for this exact board,
see Provenance below):

| Function | Pin(s) |
|---|---|
| Display (ST7701S, RGB565 parallel) | HSYNC 38, VSYNC 39, DE 40, PCLK 41, DATA0-15 on GPIO 5/45/48/47/21/14/13/12/11/10/9/46/3/8/18/17 |
| Display init (3-wire SPI, one-time) | MOSI 1, SCLK 2, CS via IO-expander |
| Backlight (PWM) | GPIO 6 |
| I2C bus (touch + IO-expander) | SDA 15, SCL 7 |
| Touch (CST820, addr 0x15) | INT on GPIO 16, reset via IO-expander |
| IO-expander (TCA9554, addr 0x20) | EXIO1 = LCD reset, EXIO2 = touch reset, EXIO3 = LCD CS, EXIO8 = buzzer |
| Buzzer | on/off only, driven through the IO-expander (not a GPIO tone) — it clicks, it doesn't pitch |
| USB | enumerates as the ESP32-S3's native USB JTAG/serial debug unit (VID 303a:1001) — flashing is plug-and-go, no BOOT button dance needed. (Waveshare's own docs describe a CH343 UART bridge instead; on the unit this was actually flashed on, it showed up as native USB, so trust what `lsusb` tells you over the datasheet.) |

Note: this board has **no onboard microphone**, so a real audio-input
chromatic tuner isn't feasible without external I2S mic hardware wired to
the three free GPIOs (0, 19, 20). CPU runs at 240MHz (bumped from the
ESP-IDF default of 160MHz — this is USB-powered, not battery-run, so
there's no reason to leave headroom on the table).

## Build

Needs the ESP-IDF toolchain (v5.1+).

```sh
. $HOME/esp/esp-idf/export.sh   # or wherever you installed ESP-IDF
cd musician-dial-alpha
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash   # port name varies
```

First build will pull `lvgl/lvgl` and `espressif/esp_lcd_touch` from the
component registry automatically (see `main/idf_component.yml`).

## Screenshots

The firmware listens on its USB serial port for a `SCREENSHOT` command and
responds with the current screen as base64-encoded RGB565. From the host:

```sh
pip install pyserial pillow
python3 tools/screenshot.py
```

Auto-detects the board by USB VID:PID, no `--port` needed unless more than
one matches. With no output path, saves a timestamped PNG under
`~/Pictures/Screenshots/esp32`.

Takes a few seconds — it's ~450KB of raw pixel data over a serial link.

## How it works

- `main/LCD_Driver`, `Touch_Driver`, `EXIO`, `I2C_Driver`, `LVGL_Driver`,
  `Buzzer` — hardware bring-up (panel init sequence, touch, IO-expander,
  LVGL glue). Adapted from Waveshare's reference implementation for this
  board; this is standard bring-up boilerplate, not the interesting part.
  App switching is driven by raw touch-coordinate tracking in
  `LVGL_Driver.c` rather than LVGL's built-in gesture system, which turned
  out to have several undocumented preconditions that made it unreliable
  on this hardware.
- `main/Metronome/metronome_engine.*` — timing engine. An `esp_timer`
  periodic callback fires once per beat, posts an event to a queue, and
  clicks the buzzer (a double-click for the downbeat, since the buzzer has
  no pitch control to distinguish it otherwise).
- `main/Metronome/metronome_ui.*` — the Metronome UI. All LVGL calls
  happen from `app_main`'s loop (the only task allowed to touch LVGL); it
  drains the beat-event queue each iteration and drives the pendulum/ring
  animation per beat.
- `main/CircleOfFifths/circle_of_fifths_ui.*` — the Circle of Fifths UI
  and its music-theory logic (diatonic scale spelling, chord derivation
  from wheel adjacency).
- `main/fonts/` — custom-generated LVGL fonts (DejaVu Sans, `lv_font_conv`)
  used everywhere instead of the bundled Montserrat: Montserrat's "G" is
  too easily mistaken for a "C" at these sizes on this display.
- `main/Debug/screenshot.c` — the `SCREENSHOT` serial command (see
  Screenshots above). Renders via `lv_snapshot_take_to_buf()` into a
  PSRAM buffer allocated directly with `heap_caps_malloc`, not LVGL's own
  allocator -- LVGL's built-in `lv_malloc` is a fixed 64KB pool
  (`CONFIG_LV_MEM_SIZE_KILOBYTES`), nowhere near big enough for a 450KB
  frame, regardless of how much PSRAM is actually free.

## Provenance

Pin assignments, the ST7701S init sequence, and the IO-expander bit map came
from `giobauermeister/waveshare-round-esp32-lvgl` on GitHub, itself a copy of
Waveshare's own ESP-IDF demo for this board. Everything in `main/Metronome`
and `main/CircleOfFifths` is original.
