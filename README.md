# TouchSky

A desktop ADS-B radar for the Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, a 7" 1024×600 touch display board.
It shows the aircraft flying around a point of your choice, live, using the free [adsb.lol](https://adsb.lol) API.
No receiver or antenna needed: only Wi-Fi.

![Radar screen: aircraft around Paris within 25 nm](screenshots/radar.png)

## Features

- Round radar scope with range rings, north up, 25 to 250 nm range
- Coastlines, borders, airports and runways for the whole of Europe, embedded in the firmware
- Aircraft drawn as arrows pointing along their track, with callsign and altitude
- Side panel: UTC clock, connection status, list of the closest aircraft
- On-screen settings saved on the device: Wi-Fi network, radar centre (latitude/longitude or airport ICAO code), range

## Hardware

- [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B)
- A USB-C **data** cable
- A 2.4 GHz Wi-Fi network with Internet access

## Build and flash

You need [ESP-IDF v5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/get-started/index.html),
installed for the `esp32p4` target. With its environment loaded (`export.sh`):

```sh
cd firmware
idf.py build
idf.py -p <port> flash monitor
```

Connect the board through the USB-C port marked **UART** and switch it on with the side switch.
The port then shows up as `/dev/cu.usbmodem*` on macOS, `/dev/ttyACM*` on Linux or `COMx` on Windows.

### Chip revision

ESP32-P4 chips before and after revision 3.0 need different binaries.
This project is configured for revisions **before 3.0** (the board it was developed on is v1.3).
`idf.py flash` prints the revision of your chip and refuses to flash a mismatched build.
For a revision 3.x chip, replace `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` in `firmware/sdkconfig.defaults` with:

```
# CONFIG_ESP32P4_SELECTS_REV_LESS_V3 is not set
CONFIG_ESP32P4_REV_MIN_300=y
```

then delete `firmware/sdkconfig` and rebuild. This variant has not been tested.

## First start

The settings screen opens on first boot:

1. Pick your Wi-Fi network (press **Scan** to refresh the list) and type its password.
2. Set the radar centre: type a latitude and longitude in decimal degrees, or an ICAO airport code (for example `LFPG`) followed by **Lookup**.
3. Choose the range and press **Save**.

The **Settings** button on the radar screen brings this screen back.

## Repository layout

| Path | Content |
|---|---|
| `firmware/` | ESP-IDF project (LVGL 9, Waveshare board support package) |
| `tools/` | `build_map.py`, which generates the embedded map |
| `tests/` | Host unit tests, tool tests and an on-device check |
| `sim/` | Host simulator that renders the screens to image files |

## Regenerating the map

`firmware/main/map.bin` is committed, so this is only needed to change the covered area or the level of detail
(`EUROPE` and `TOLERANCE_DEG` in the script). It needs Python 3 and downloads its sources on the first run:

```sh
python3 tools/build_map.py
```

## Tests

```sh
tests/run.sh                    # unit tests on your computer (needs CMake, Ninja, Python 3 and IDF_PATH)
tests/run.sh device <port>      # flashes the board and checks that it boots
tests/run.sh device <port> map display wifi adsb   # also waits for Wi-Fi and a first API answer
```

The firmware logic (projection, API parsing, map reader, settings validation) is plain C without any ESP-IDF dependency,
which is what lets the first command run without a board.

## Simulator

`sim/` builds the firmware's real screen code for your computer and saves screenshots, without a board.
It needs CMake, Ninja and the ESP-IDF environment, and downloads LVGL on the first run:

```sh
sim/run.sh                              # Paris, 25 nm, with a captured API answer
sim/run.sh 51.47 -0.45 50 traffic.json  # your own centre, range and adsb.lol answer
```

Screenshots of the radar and settings screens land in `sim/out/`.
To use live traffic, save an API answer first:
`curl -o traffic.json https://api.adsb.lol/v2/point/51.47/-0.45/50`.

With [SDL2](https://www.libsdl.org) installed (`brew install sdl2`), the simulator can also open a live window,
where the mouse acts as the finger on the touch screen:

```sh
sim/run.sh --window                          # captured response, Paris, 25 nm
sim/run.sh --window 51.47 -0.45 50 live      # live traffic from adsb.lol, refreshed every 5 s
```

The captured response is shown once and does not refresh, so aircraft only move as far as the firmware extrapolates them.

## Data sources and credits

- Live aircraft positions: [adsb.lol](https://adsb.lol), a community-run ADS-B network
- Coastlines and borders: [Natural Earth](https://www.naturalearthdata.com), public domain
- Airports and runways: [OurAirports](https://ourairports.com/data/), public domain
- Board support package and examples: [waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-7B)

## License

[GPL-3.0](LICENSE)
