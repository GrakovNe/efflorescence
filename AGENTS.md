# AGENTS.md

Notes for coding agents: the bench, the recipes, and the constraints
that are not visible in the code.

## Bench

- Board: Seeed Studio XIAO ESP32-C3 (ESP32-C3 rev v0.4, 4 MB embedded
  flash). MAC `44:b1:76:5d:c6:e0`, device id `c6e0`. Native
  USB-Serial/JTAG; the port is `/dev/cu.usbmodem*` and its name changes
  with the USB socket (`101`, `2101`), so glob for it.
- Power: Li-ion 4000 mAh through a TP4056 module into VUSB. **USB may be
  plugged in only with the battery disconnected.** In the case there is
  no USB and no BOOT/RESET button; the touch sensor is the only control.
- Touch sensor TTP223, powered from GPIOs: module GND = GPIO3 driven low,
  I/O = GPIO4 (high while touched), VCC = GPIO5 driven high.
- LED: 3 W, switched on the low side by a MOSFET module with an
  optocoupler input. Module GND = GPIO6 driven low, PWM = GPIO7 (high =
  LED on). LED plus goes to the supply plus, LED minus to LOAD-.
- No battery voltage divider and none will be added.
- ESP-IDF v5.5.5 in `~/esp/esp-idf`. `esptool` v5 via pipx; the same
  venv has pyserial: `~/.local/pipx/venvs/esptool/bin/python`.
- Flash reads over this USB work (`read-flash` of 4 MB takes 17 s).

## Recipes

Build:

```bash
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf5.5_py3.14_env
source ~/esp/esp-idf/export.sh
IDF_SKIP_CHECK_SUBMODULES=1 idf.py build
```

`IDF_PYTHON_ENV_PATH` is needed because the default `python3` is 3.15
and the only installed IDF environment is the 3.14 one; without it
`export.sh` fails and `idf.py` is not found. `IDF_SKIP_CHECK_SUBMODULES=1`
skips a git check that needs the network. After editing
`main/Kconfig.projbuild` or adding a component: `idf.py reconfigure`.

Flash: `idf.py -p /dev/cu.usbmodemXXXX flash` (bootloader, partition
table, otadata, app). `app-flash` keeps settings in `nvs`. `erase-flash`
gives the factory state.

Console capture (opening the port resets the board, so every capture
starts at boot and then follows the running device):

```bash
~/.local/pipx/venvs/esptool/bin/python - /dev/cu.usbmodemXXXX <<'EOF'
import serial, sys, time, re
s = serial.Serial()
s.port = sys.argv[1]; s.baudrate = 115200; s.timeout = 0.3
s.dtr = False; s.rts = False
s.open()
t0 = time.time(); buf = b''
while time.time() - t0 < 20:
    buf += s.read(8192)
print(re.sub(r'\x1b\[[0-9;]*m', '', buf.decode('utf-8', 'replace')))
EOF
```

Host tests: `cd test/host && make test`.

## Constraints

- Structure, style and (later) most network modules come from
  `../esp32-hydro-temp`; keep them in sync in spirit. SPDX header in
  every source file, C11, 4 spaces, `snake_case`. Comments only for what
  the code does not say. Commits in English, body says why.
- Logic that does not need hardware is pure and host-tested: `dimmer`
  (brightness curve), `lamp_core` (state, fades, hold ramp, signals),
  `gesture` (taps, holds, series, the reset sequence). `lamp.c` and
  `touch.c` only move it onto LEDC, GPIO, NVS and tasks.
- Brightness is a percent 1..100 of *perceived* light. `lamp_core` works
  in levels 0..65535 on the same perceptual scale; only `dimmer_duty()`
  turns a level into PWM duty (CIE 1931 lightness). Level 0 is off,
  every other level maps into `DUTY_MIN..DUTY_MAX`.
- Only the lamp task writes LEDC. Everything else goes through the
  `lamp_*()` calls, which take the lock and wake the task.
- State (on, brightness) is saved to NVS 5 s after the last change, never
  on every step. Only `settings.c` touches NVS. `reset_early_init()` runs
  before anything opens NVS.
- A tap acts on release and at once; series are counted in parallel, so
  the five taps of the reset sequence do toggle the lamp five times.
  Factory reset fires on the release after a 5 s hold, not while the
  finger is still on the sensor.
- The touch task samples only while a gesture is in flight and otherwise
  sleeps on a GPIO edge interrupt. Keep it that way: constant polling
  would defeat light sleep.
- The partition table has two OTA slots and the bootloader is built with
  app rollback. Neither can be changed over the air, and the device is
  sealed, so do not change them casually.
- PWM is 1250 Hz, 14 bit. The optocoupler is the slow part: a PC817 into
  roughly 10 kΩ has 55-70 µs edges (Sharp datasheet, Fig. 13). Raising
  the frequency shortens the usable range from the bottom. The lowest
  usable duty (`APP_LED_DUTY_MIN_PERMILLE`, 1 % now) is a guess that has
  not been measured on this lamp.

## Agreed with the owner, not built yet

- Wi-Fi stays associated, auto light sleep while the lamp is off; no
  sleep while the LED is on. Setup AP lives 5 minutes; without settings
  the radio then goes off, with settings the station keeps retrying. A
  hold on the sensor brings the radio back.
- Web: on/off, brightness, soft on/off switch, schedule on the device
  (time, weekdays, on/off, brightness, transition; applied live, no
  restart), timezone. API without authentication, by decision.
- MQTT: `<prefix>/set`, retained `<prefix>/state`, `<prefix>/get`, Home
  Assistant discovery (`light`, a `battery_low` binary sensor, an
  `update` entity), tap series as events.
- OTA: update check against the GitHub Pages manifest, dev upload at
  `POST /api/v1/ota`, version `v1.0.<run>` baked into the image.
- Battery: only "discharged" is detected, by the brownout reset reason;
  after it the lamp boots off with a `battery_low` flag, and repeated
  brownouts end in deep sleep until a touch.
- Logs over the network (ring buffer and `/api/v1/log`).
