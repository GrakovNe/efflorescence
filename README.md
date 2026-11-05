# Efflorescence

A battery lamp: a 3 W LED under an acrylic ball with flowers in it, one
touch sensor, and an ESP32-C3 that dims it, keeps a schedule with a
sunrise, and talks to a phone or to Home Assistant over Wi-Fi.

## Hardware

- Seeed Studio XIAO ESP32-C3.
- TTP223 touch sensor module.
- A 3 W LED with its series resistor, switched on the low side by a
  MOSFET module with an optocoupler input.
- A Li-ion cell with a TP4056 charger module on the board's 5 V pin.
  Never plug in USB with the battery connected.

| XIAO pin | GPIO | Goes to |
| --- | --- | --- |
| D1 | 3 | sensor GND (the board drives it low) |
| D2 | 4 | sensor I/O |
| D3 | 5 | sensor VCC (the board drives it high) |
| D4 | 6 | MOSFET module GND (driven low) |
| D5 | 7 | MOSFET module PWM |
| 5V, GND | | charger module OUT+, OUT- |

The LED's plus goes to the battery plus, its minus to the module's
LOAD-. Pins can be changed in `idf.py menuconfig` → Efflorescence.

## Install a ready-made firmware

A new board, over USB: open <https://grakovne.github.io/efflorescence/>
in Chrome or Edge, plug the board in, press Install.

A lamp that already runs Efflorescence: Settings → Firmware → Check for
updates. The files are also on the
[Releases](https://github.com/GrakovNe/efflorescence/releases) page:
`efflorescence-full.bin` is the whole flash for address 0x0,
`efflorescence-app.bin` is what a running lamp takes.

## Build from source

You need ESP-IDF 5.5.

```bash
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash
```

Tests:

```bash
cd test/host && make test
node test/ui/ui_smoke.js
```

To publish a release, run the Release workflow on GitHub
(`gh workflow run release.yml`). It builds the firmware as
`v1.0.<run number>`, attaches it to a new release and updates the
install page and the update manifest that the lamps read.

## Use

### The sensor

- Tap: on or off.
- Hold: brighter or dimmer; the direction turns around with every hold.
  Holding a dark lamp turns it on at its dimmest.
- Five taps, then hold for five seconds: factory reset. The lamp blinks
  slowly while you hold and quickly once it is ready; letting go earlier
  cancels.

### Wi-Fi

A new lamp is the open network `Efflorescence-XXXX`. Join it from your
phone, pick your home Wi-Fi and save. After a restart the lamp is at
`http://efflorescence-xxxx.local/`. Renaming it in Settings changes the
`.local` name too.

The setup network costs battery, so it lasts five minutes. Hold the
sensor to bring it back. A lamp that has lost its home network does the
same and keeps looking for the network on its own.

### The pages

- Dashboard: on, off, brightness.
- Schedule: any number of entries up to twelve, each with a start time,
  weekdays, on or off, a brightness and a fade. A sunrise is an "on"
  entry with a long fade; the page shows when it reaches its brightness.
  Changes apply at once. The schedule needs the clock, which the lamp
  sets from the internet; set the time zone in Settings.
- Settings: Wi-Fi, soft on and off, MQTT, name, time zone, firmware.
- Device Info.

### Battery

The lamp cannot measure its battery. It notices only when the battery is
empty: it then starts dark, shows "battery low" on its pages and over
MQTT, and refuses firmware updates until the light has stayed on for
five minutes after a charge. If the battery cannot even keep it awake,
the lamp goes to sleep until the sensor is touched.

While the light is off the lamp dozes and stays on Wi-Fi.

### API

| Method | Path | |
| --- | --- | --- |
| GET | `/api/v1/light` | state |
| POST | `/api/v1/light` | `{"state": "ON"\|"OFF"\|"TOGGLE", "brightness": 1..100, "transition": seconds, "soft": bool}`, every field optional |
| GET | `/api/v1/schedule` | the schedule, the lamp's clock, what is next |
| POST | `/api/v1/schedule` | replace the schedule |
| GET | `/api/v1/status` | device state |
| GET | `/api/v1/config` | settings |
| POST | `/api/v1/config` | change settings, the lamp restarts |
| GET | `/api/v1/wifi/scan` | visible Wi-Fi networks |
| GET | `/api/v1/ota` | firmware version and update state |
| POST | `/api/v1/ota` | install the image in the request body |
| POST | `/api/v1/ota/check` | look for a published update |
| POST | `/api/v1/ota/install` | install the update found |
| GET | `/api/v1/log` | the last lines of the device log |
| POST | `/api/v1/reboot` | restart |
| POST | `/api/v1/factory-reset` | erase settings |

No password anywhere, firmware updates included, so keep the lamp on a
trusted network.

### MQTT

Turn MQTT on in Settings and enter the broker address. Under the topic
prefix, `efflorescence/<id>` by default:

| Topic | |
| --- | --- |
| `set` | a command: the JSON of `POST /api/v1/light`, or just `ON`, `OFF`, `TOGGLE` |
| `state` | retained: `{"state": "ON", "brightness": 60, "soft": true, "battery_low": false}` |
| `get` | any message makes the lamp publish its state |
| `availability` | `online` or `offline` |
| `event` | `{"event_type": "double", "count": 2}` after two or more taps in a row |
| `update` | installed and latest firmware version |
| `update/install` | `install` installs the latest |

Home Assistant discovery is on by default: the lamp shows up as a light
with brightness and transitions, a switch for soft on and off, an event
for taps, a battery sensor and a firmware update.

### Updates

A new firmware goes into the second half of the flash and has three
minutes to get back on the network. If it does not, or if the lamp
restarts before that, the previous firmware comes back by itself.

## License

MIT, see `LICENSE`.
