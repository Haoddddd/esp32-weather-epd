# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Arduino-framework firmware (C++17) for an ESP32 + 7.5"/7.3" e-paper weather display. It fetches OpenWeatherMap One Call 3.0 and Air Pollution data, reads an indoor BME280/BME680 sensor, renders one full-screen refresh, then deep-sleeps. The PlatformIO project lives in `platformio/`; everything outside it is hardware docs, asset sources, and generator tooling.

## Commands

All firmware commands run from `platformio/` (or pass `-d platformio`). There is no test suite or linter; CI (`.github/workflows/build.yaml`) only runs `pio run` on Linux/macOS/Windows, so a clean compile is the verification bar.

```sh
pio run -d platformio                                   # build default env (dfrobot_firebeetle2_esp32e)
pio run -d platformio -e firebeetle32                   # build the other board env
pio run -d platformio -t upload                         # flash over USB
pio device monitor -d platformio                        # serial monitor (115200 baud)
```

Build flags are `-Wall -std=gnu++17`; partition table is `huge_app.csv` and CPU is pinned to 80 MHz for power. Dependency versions are pinned in `platformio.ini`.

Because configuration is compile-time macros, changes to conditional code should be compiled against the relevant macro combinations (e.g. each `DISP_*` panel, `SENSOR_BME280` vs `SENSOR_BME680`, each `USE_HTTP*` mode, a CJK vs non-CJK `LOCALE`), not just the defaults.

CJK font tooling (see `fonts/cjk/README`):
```sh
python3 fonts/cjk/gen_cjk_fonts.py      # regenerate CJK fonts after changing zh_CN strings or CITY_STRING
bash fonts/cjk/preview/run_preview.sh   # host build of src/cjk_text.cpp + self-test + sample PNG
```

`platformio/src/config.cpp` holds WiFi credentials, the API key, and location. Real values are kept locally and must never be committed; carry them across branches with `git stash`.

## Architecture

**Single-shot execution model.** Everything happens in `setup()` in `src/main.cpp`; `loop()` is empty. Each wake: read battery → connect WiFi → NTP sync → HTTP requests → read sensor → render → `beginDeepSleep()`. Every failure path draws an error screen via `drawError()` and goes straight to deep sleep. Deep sleep wake times are aligned to multiples of `SLEEP_DURATION` relative to `WAKE_TIME`, and skip the `BED_TIME`–`WAKE_TIME` window. The only state persisted across wakes is the `lowBat` flag in NVS (`Preferences`, namespace `NVS_NAMESPACE`), used so the low-battery screen is drawn only once.

**Configuration is split in two:**
- `include/config.h` — feature/hardware selection as mutually-exclusive `#define`s (panel, driver board, sensor, locale, units, HTTP mode, wind indicator, widget positions, font, debug level). The bottom of the file has XOR-based `#error` validation; any new exclusive option group needs a matching validation block there.
- `src/config.cpp` — runtime-ish constants declared `extern` in `config.h` (pins, WiFi credentials, API key, lat/lon, timezone, time formats, sleep schedule, battery thresholds).

**Module responsibilities:**
- `client_utils` — WiFi start/kill, SNTP sync, `getOWMonecall` / `getOWMairpollution` HTTP calls.
- `api_response` — `owm_*_t` structs and ArduinoJson deserializers. One Call is parsed directly from the `WiFiClient` stream using a `DeserializationOption::Filter` to keep RAM low; response structs are `static` globals in `main.cpp` because they are too large for the stack.
- `renderer` — the global GxEPD2 `display` object (its template type is chosen by the `DISP_*` macro, which also sets `DISP_WIDTH`/`DISP_HEIGHT`) and all `draw*` functions for each screen region. Drawing uses GxEPD2 paged mode: callers wrap draw calls in `do { ... } while (display.nextPage());`.
- `display_utils` — pure helpers: battery ADC/percent, icon bitmap selection by condition/size, alert filtering/categorization, UV/WiFi descriptions, HTTP status phrases, compass notation.
- `conversions` — unit conversions selected by `UNITS_*` macros.
- `_strftime` — custom `strftime` that uses the locale's `LC_*` strings; supports the GNU `-` no-padding flag (`%-d`).
- `cjk_text` — UTF-8 decoding and drawing of mixed Latin/CJK strings (see below).

**Localization.** `src/locale.cpp` token-pastes the `LOCALE` macro to `#include "locales/locale_<code>.inc"`. Each `.inc` defines every symbol declared `extern` in `include/_locale.h` (`LC_*` time strings, `OWM_LANG`, `TXT_*` UI strings, AQI/alert terminology). Adding a string means adding the extern to `_locale.h` **and** a definition in every locale file, otherwise the link fails for that locale. New locales are added by copying an existing `.inc` and listing it in the comment in `config.h`. Locale files use CRLF line endings; non-CJK locales encode non-ASCII as ISO-8859-1 octal escapes (e.g. `"\260C"`), while `zh_CN` uses UTF-8.

**CJK text.** Adafruit GFX fonts are 8-bit (ISO-8859-1), so locales listed as `LOCALE_USES_CJK_<code>` in `include/cjk_text.h` set `USE_CJK_TEXT`, which compiles in a second rendering path; other locales compile none of it. In `renderer.cpp`, always call the `setFont()` wrapper rather than `display.setFont()`: it records the active Latin font and picks its paired CJK font from `CJK_FONT_PAIRS`. `drawString`/`getStringWidth`/`drawMultiLnString` switch to the CJK path when a string contains a code point above U+00FF; that path draws Latin-1 characters with the GFX font and the rest from subsetted CJK bitmap fonts (`lib/esp32-weather-epd-assets/fonts/cjk/`, generated, containing only characters found in string literals of `locale_zh_CN.inc`, `config.cpp`, and optional `fonts/cjk/extra_chars.txt`). Characters missing from the generated fonts render as blank space, so regenerate after adding strings. CJK lines may break between any two characters.

**Assets are generated C headers, not edited by hand.**
- Icons: `platformio/lib/esp32-weather-epd-assets/icons/<N>x<N>/*.h` are produced from `icons/svg/` via `cd icons && make` (requires Inkscape + Python 3; do not use `make -j`, Inkscape breaks in parallel). Output lands in `icons/icons/` and must be moved into the assets lib manually.
- Fonts: `platformio/lib/esp32-weather-epd-assets/fonts/` are produced from `fonts/ttf/` via `bash fonts/ttf_to_adafruit_gfx.sh` (requires fonttools/pyftsubset, freetype2, and the bundled Adafruit `fontconvert`). Each font gets an ISO-8859-1 subset plus a digits-only `_temperature` subset for the large temperature readout. The active font is selected by `FONT_HEADER` in `config.h`; layout is tuned for FreeSans.
- The assets lib sets `libLDFMode: off`, so its headers are included by path (e.g. `"icons/icons_196x196.h"`, `FONT_HEADER`).

**TLS certificates.** With `USE_HTTPS_WITH_CERT_VERIF` (default), `include/cert.h` pins the OpenWeatherMap CA chain and will eventually expire. Regenerate with:
```sh
cd cert && python cert.py -s api.openweathermap.org -p 443 -n openweathermap > cert.h
```
then copy into `platformio/include/cert.h` and check that the cert name used in `client.setCACert(...)` in `main.cpp` still exists.

## Conventions

- Source files carry a GPL-3.0 header. Upstream files credit `Luke Marzen`; files added in this fork credit `Haodong Zhao`.
- Style: 2-space indent, Allman braces, `// end <function>` comments after function bodies, ~80-column lines.
- Serial output goes through locale `TXT_*` strings; extra verbosity is gated by `DEBUG_LEVEL` (`>= 1` heap stats, `>= 2` raw API responses).
- Adding a new panel touches `config.h` (macro + validation), `renderer.h`/`renderer.cpp` (display type and constructor), and any layout code gated on resolution or color count.
