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
source ~/esp/esp-idf/export.sh
IDF_SKIP_CHECK_SUBMODULES=1 idf.py build
```

The IDF Python environment is the 3.15 one
(`~/.espressif/python_env/idf5.5_py3.15_env`). A build directory
configured under another Python refuses to build until `idf.py
fullclean`. `IDF_SKIP_CHECK_SUBMODULES=1`
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

Host tests: `cd test/host && make test` (the pure modules; the two that
parse JSON compile cJSON from the ESP-IDF checkout) and
`node test/ui/ui_smoke.js` (the three pages against mocked API answers).

The device without Wi-Fi settings is the open AP `Efflorescence-C6E0`
for five minutes after power-up; a hold on the sensor brings it back.
The Mac cannot join it from an agent session, so first provisioning is
done from a phone. After that the device answers at
`http://efflorescence-c6e0.local/`:

```bash
curl http://efflorescence-c6e0.local/api/v1/status
curl -d '{"state":"ON","brightness":40,"transition":2}' \
     http://efflorescence-c6e0.local/api/v1/light
# bench: raw duty and PWM frequency, then hand the LED back
curl -d '{"duty":200,"freq_hz":400}' http://efflorescence-c6e0.local/api/v1/debug/pwm
curl -d '{"duty":null}' http://efflorescence-c6e0.local/api/v1/debug/pwm
```

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
- One command format for HTTP and (later) MQTT, parsed by the pure
  `light_cmd.c`: `{"state": "ON"|"OFF"|"TOGGLE", "brightness": 1..100,
  "transition": seconds, "soft": bool}`, every field optional, a superset
  of the Home Assistant JSON light schema. The state goes out as
  `{"state", "brightness", "soft"}` from `light_state_json()`.
- A config POST saves and restarts, as in the reference. The one
  exception is `soft` (soft on/off): it also changes live through a light
  command, and then `lamp` updates `cfg` and writes the single NVS key.
  The config POST merge is `config_merge.c` (pure, host-tested);
  `web_server.c` only moves bytes.
- Only `wifi_mgr` calls `esp_wifi_*`, and mode changes run on its task.
  An AP cannot sleep, so it lives 5 minutes (`AP_LIFETIME_US`) unless a
  phone is on it. Without Wi-Fi settings the radio then stops (`off`);
  with settings the AP goes down and the station retries every 3
  minutes. `wifi_mgr_wake()`, called on a hold of the sensor, brings the
  AP back for another lifetime.
- The API has no authentication and CORS `*` by decision of the owner,
  OTA included. Secrets are reported only as `*_set` booleans.
- Pages are self-contained HTML, no CDN: the captive portal has no
  internet. Style: flat, one accent `#FF6F3F`, hairlines, uppercase
  section labels, no cards, no explanatory copy.
- `managed_components/` is downloaded; do not edit it.
- The LED has series resistors and the MOSFET module is fed from the
  battery, below the voltage it is meant for, so the LED gets dimmer as
  the battery drains. The owner knows and accepts it.
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

- Power: auto light sleep while the lamp is off and Wi-Fi stays
  associated; no sleep while the LED is on.
- Schedule on the device (time, weekdays, on/off, brightness,
  transition; many entries; applied live at `/api/v1/schedule`, no
  restart), timezone. A sunrise is an "on" entry with a long transition.
  Missed events are not replayed.
- MQTT: `<prefix>/set`, retained `<prefix>/state`, `<prefix>/get`, Home
  Assistant discovery (`light`, a `battery_low` binary sensor, an
  `update` entity), tap series as events.
- OTA: update check against the GitHub Pages manifest, dev upload at
  `POST /api/v1/ota`, version `v1.0.<run>` baked into the image.
- Battery: only "discharged" is detected, by the brownout reset reason;
  after it the lamp boots off with a `battery_low` flag, and repeated
  brownouts end in deep sleep until a touch.
- Logs over the network (ring buffer and `/api/v1/log`).
