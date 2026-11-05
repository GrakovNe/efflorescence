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
- On the home Wi-Fi the lamp is `efflorescence-c6e0.local`, on
  2026-11-05 at `192.168.1.91`. Use `curl -4` or the IP: plain `curl`
  asks for an IPv6 address of a `.local` name first and stalls for 5 s
  on every request.

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

Flash over the air, which is the only way once the lamp is in its case
(the image goes into the other slot and the lamp restarts into it):

```bash
curl --data-binary @build/efflorescence.bin http://192.168.1.91/api/v1/ota
curl http://192.168.1.91/api/v1/ota     # version, slot, pending_verify
```

The published-update path without a release: serve a manifest from this
machine and point one check at it.

```bash
mkdir /tmp/ota && cp build/efflorescence.bin /tmp/ota/efflorescence-app.bin
echo '{"version":"v9.9.9","app":"efflorescence-app.bin"}' > /tmp/ota/ota.json
(cd /tmp/ota && python3 -m http.server 8765 --bind <this machine's IP>) &
curl -d '{"url":"http://<this machine's IP>:8765/ota.json"}' http://192.168.1.91/api/v1/ota/check
curl -X POST http://192.168.1.91/api/v1/ota/install
```

A release build by hand, to rehearse what the workflow does:
`APP_VERSION=v1.0.1 idf.py -B build-rel -DSDKCONFIG=build-rel/sdkconfig
build`. Do not leave a lamp on such a build: it would think it already
has the real `v1.0.1`.

Rollback bench build (its own build directory, because the CMake cache
keeps `-D` flags): an image that never confirms itself. Upload it and
the lamp comes back on the previous image 180 s later, or at once after
any restart.

```bash
IDF_SKIP_CHECK_SUBMODULES=1 idf.py -B build-rb \
  -DSDKCONFIG=build-rb/sdkconfig -DAPP_OTA_TEST_NO_CONFIRM=1 build
curl --data-binary @build-rb/efflorescence.bin http://192.168.1.91/api/v1/ota
```

MQTT: the broker is `192.168.1.102:1883`, anonymous; the lamp's prefix
is `efflorescence/c6e0`.

```bash
mosquitto_sub -h 192.168.1.102 -v -t 'efflorescence/#' \
  -t 'homeassistant/+/+/config' | grep -E '^efflorescence/|efflorescence_c6e0'
mosquitto_pub -h 192.168.1.102 -t efflorescence/c6e0/set -m TOGGLE
mosquitto_pub -h 192.168.1.102 -t efflorescence/c6e0/set \
  -m '{"state":"ON","brightness":40,"transition":2}'
```

Logs without the console, which the case hides and light sleep kills:
`curl http://192.168.1.91/api/v1/log` gives the last 8 KB.

Where the time goes (there is no ammeter on the bench):
`curl http://192.168.1.91/api/v1/debug/pm` prints the power management
locks and the time spent in each mode since boot. Take it twice and
subtract. Measured on 2026-11-06 with the lamp off on the home network
(beacon every 102 ms, DTIM 1): 87 % in light sleep, the radio awake
11 %, 12.8 ms per beacon. Listening to every third beacon
(`APP_WIFI_LISTEN_INTERVAL=3`) gave 91 % and 7 %, and the owner chose
it: it is the default. The internal 17.5 MHz/256 sleep clock changed
nothing.

Battery handling cannot be provoked on USB power, so it is simulated:

```bash
curl -d '{"simulate":"brownout"}' http://192.168.1.91/api/v1/debug/battery
curl -d '{"simulate":"clear"}'    http://192.168.1.91/api/v1/debug/battery
# deep sleep until the sensor is touched; the lamp leaves the network
curl -d '{"simulate":"sleep"}'    http://192.168.1.91/api/v1/debug/battery
```

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
- `lamp_core` works in levels 0..65535 of perceived lightness (CIE L*,
  65535 = 100); only `dimmer_duty()` turns a level into PWM duty. Level
  0 is dark. Brightness 1..100 % spans `floor`..65535 evenly, where the
  floor is the level whose duty is `APP_LED_DUTY_MIN_PERMILLE`. Below
  the floor the light flickers, so the lamp never rests there.
- The soft switch (`LAMP_TRANSITION_SOFT`) takes time in proportion to
  how far the light has to go: 700 ms across the whole range, almost
  nothing from a dim lamp. A fixed time made a dim lamp sit still and
  then drop, and so did an S-curve at any brightness: the owner saw both
  as "no reaction, then a sudden off". Going down the light moves at
  once and at a steady pace; going up it eases into the brightness.
- The stretch between dark and the floor is crossed in a straight line
  in at most 80 ms (`LAMP_TRANSIT_MS`), at the start of a fade in and at
  the end of a fade out. A transition given in seconds (API, MQTT,
  schedule) is a straight line in lightness over that time, so a
  30-minute sunrise starts at the floor after a blink.
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
- Only `ota.c` calls `esp_ota_*` and `esp_https_ota_*`; the rules it
  follows (version order, the manifest, when a new image counts as good)
  are the pure `ota_logic.c`. A new image is on probation until the way
  the next update would arrive is open again: station connected and the
  web server up, or, without Wi-Fi settings, the setup AP up for 20 s.
  After 180 s without that it rolls itself back. Verified on the lamp on
  2026-11-05, both by the timer and by a restart during probation.
- An image is accepted only if its project name equals the running
  one. Renaming `project(efflorescence)` therefore locks every lamp in
  the field out of updates.
- The version is `PROJECT_VER`: `APP_VERSION` from the environment in a
  release build (`v1.0.<run>`), `git describe` otherwise. Only versions
  of the form `v1.0.123` count as releases; a development build is
  offered any release and never offered as one.
- MQTT topics under `<prefix>/`: `set` (a light command as JSON, or the
  bare words ON, OFF, TOGGLE), `state` (retained), `get` (any payload
  republishes the state), `availability` (online, offline by last
  will), `event` (a finished series of two or more taps, not retained),
  `update` (retained, the Home Assistant update payload) and
  `update/install` (payload `install`). Discovery announces a light, a
  soft on/off switch, a taps event and a firmware update entity;
  `unique_id`s are bound to the device id, never to the name, so a
  rename keeps the entities. Turning discovery off removes them. The
  payloads are built by the pure `ha_discovery.c`.
- `esp_mqtt_client_publish/subscribe` only from the `mqtt_pub` task. The
  esp-mqtt event handler only sets flags and posts to its queue; a light
  command received there is applied by `mqtt_pub`, not in the handler.
  State is published at most every 250 ms: a held finger changes the
  brightness a hundred times a second.
- The lamp looks for a release by itself two minutes after boot and then
  once a day, so Home Assistant can offer it; a failed automatic check
  leaves no error behind.
- The schedule is the pure `schedule.c` (validation, what is due, what
  is next, JSON both ways); `sched_mgr.c` only ticks once a minute and
  stores the list as one NVS blob. `POST /api/v1/schedule` replaces the
  whole list and takes effect at once, no restart. An entry's time is
  when its action *starts*; a sunrise is an "on" entry with a long
  transition and the page shows when it ends. An entry fires only in its
  own minute (a tick up to two minutes late still runs it); anything
  older is missed and stays missed. Entries due in the same minute run
  in list order, so the later one wins.
- The device has no zone database: the zone is a POSIX TZ string in
  `cfg->tz`, picked on the settings page from a table of presets. The
  clock counts as set after an NTP sync or when the RTC kept it across a
  restart (seen in the boot log: "clock kept across the restart"); after
  a power loss the schedule waits for NTP.
- Automatic light sleep is on. The lamp task holds a NO_LIGHT_SLEEP lock
  (`power_led_active`) while the LED shows anything, because sleep stops
  the PWM clock; with the LED dark the chip dozes between beacons and
  Wi-Fi stays associated. Nothing in the firmware may poll on a short
  period: every task waits on an event or a long timeout.
- Light sleep floats every pin it is not told to keep. The five pins in
  use (sensor power, sensor output, LED driver ground and PWM) are
  exempted with `gpio_sleep_sel_dis()`; a new pin needs the same.
- The sensor interrupt is level-triggered (edges are not seen in light
  sleep) and is also the wake-up source. The ISR turns it off; the touch
  task turns it back on once the gesture is over.
- While the lamp is off the USB console is gone, and so is the USB port
  as far as esptool is concerned: turn the lamp on, or hold BOOT while
  resetting, before flashing over USB.
- The battery cannot be measured. A brownout reset (threshold 2.92 V on
  the 3.3 V rail) is the only sign of an empty one: the lamp then starts
  dark with `battery_low` set, refuses firmware updates, and after three
  brownouts in a row goes into deep sleep until the sensor is touched.
  The flag lives in RTC memory, so it survives those resets and is gone
  when the supply is; it clears itself once the LED has been on for five
  minutes. The rules are the pure `battery_logic.c`. None of this has
  been seen with a real empty battery, only with the bench simulation.
- PWM is 1250 Hz, 14 bit. The optocoupler is the slow part: a PC817 into
  roughly 10 kΩ has 55-70 µs edges (Sharp datasheet, Fig. 13). Raising
  the frequency shortens the usable range from the bottom. The floor,
  2.9 % duty (23 µs pulses), was found by eye by the owner: it is what
  brightness 15 % was on the scale used before 2026-11-06.

## Written, never run

- `.github/workflows/release.yml`: builds with `APP_VERSION=v1.0.<run>`
  (or the pushed `v*` tag), merges the full image, and publishes
  `efflorescence-full.bin`, `efflorescence-app.bin`, the ESP Web Tools
  `manifest.json` and the `ota.json` the lamps read to GitHub Pages at
  the URL in `APP_UPDATE_URL`. The repository `GrakovNe/efflorescence`
  does not exist yet; once it does, Pages has to be set to "GitHub
  Actions". What it does was rehearsed by hand on 2026-11-06: a build
  with `APP_VERSION=v1.0.1`, a manifest served from this machine, and
  the lamp updating itself from it.
