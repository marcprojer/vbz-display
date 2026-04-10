# VBZ Display

Live Zurich transit departure board on a 128x64 HUB75 LED matrix panel.

## Overview

- **Hardware**: ESP32 + HUB75 128x64 RGB LED panel
- **Data**: Live departures from Escher-Wyss-Platz via `transport.opendata.ch` API
- **Features**: WiFi-enabled, NTP sync, color-coded line badges, departure filtering, Home Assistant integration

## Quick Start

1. **Dependencies** in `lib/`:
   - ESP32-HUB75-MatrixPanel-DMA
   - Adafruit-GFX-Library
   - ArduinoJson
   - ArduinoOTA

2. **Configuration** (`main/config.h`):
   - WiFi SSID/password
   - Static IP
   - Home Assistant token (optional)

3. **Compile & Upload**:
   ```bash
   platformio run -t upload
   ```

## Features

- **Display**: 5-row scrollable departure board with line colors, times, destinations
- **Modes**: All / Tram / Bus filtering
- **API**: ~30 departures fetched, 15 displayed (scrollable)
- **Home Assistant**: REST endpoints for mode, wake/sleep, scroll, state
- **Wake Timer**: Configurable auto-sleep (default 5 min)

## UI Indicators

- `∞` (0 min): Immediate departure (glyph `\x1E`)
- `` (space): Live time
- `'` (apostrophe): Planned time
- `>` (delay marker): Available in serial output

## Files

- `main.ino`: WiFi, NTP, API fetch, polling loop
- `display.h`: Panel rendering, text layout, scrolling
- `homeassistant.h`: REST API endpoints
- `config.h`: Local settings, WiFi, pins

## Links

- [Data Source](https://transport.opendata.ch/docs.html)
