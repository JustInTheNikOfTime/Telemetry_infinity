#!/usr/bin/env python3
"""
Generate Arduino IDE-ready, single-file sketches from the PlatformIO sources.

Outputs:
  arduino_ide/flight_sketch/flight_sketch.ino   -> upload to the flight ESP32
  arduino_ide/ground_sketch/ground_sketch.ino   -> upload to the ground-station ESP32
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "rocket_altd_src"


def read(rel: str) -> str:
    return (SRC / rel).read_text(encoding="utf-8")


def clean(text: str, drop_system_includes=("<Arduino.h>",)) -> str:
    """Strip pragma-once guards, local header includes and Arduino.h."""
    out = []
    for line in text.splitlines():
        s = line.strip()
        if s == "#pragma once":
            continue
        if s.startswith("#include"):
            target = s[len("#include"):].strip()
            if target.startswith('"'):
                continue
            if target in drop_system_includes:
                continue
        out.append(line)
    return "\n".join(out)


# ---------------- flight sketch ----------------
config = clean(read("include/Config.h"))
protocol = clean(read("include/Protocol.h"))
sensor = clean(read("src/flight/SensorFilter.h"))
flash = clean(read("src/flight/FlashLogger.h"))
persist = clean(read("src/flight/Persist.h"))
# Unused default arg would trip the Arduino preprocessor's auto-prototypes.
flash = flash.replace("float batt_v = 0.0f)", "float batt_v)")
flight_main = clean(read("src/flight/main.cpp"))

wdt_old = """    // Setup Watchdog Timer
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);"""
wdt_new = """    // Setup Watchdog Timer (compatible with arduino-esp32 core 2.x and 3.x)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_cfg);
#else
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
#endif
    esp_task_wdt_add(NULL);"""
assert wdt_old in flight_main, "WDT block not found in flight main.cpp"
flight_main = flight_main.replace(wdt_old, wdt_new)

flight_ino = "\n".join([
    "// ============================================================",
    "// ROCKET FLIGHT COMPUTER - Arduino IDE sketch",
    "// Auto-generated from the PlatformIO sources. Upload to the",
    "// ESP32 WROOM wired to BMP280 (I2C 21/22) + SX1278 (SPI).",
    "// Board: ESP32 Dev Module | Partition: Default 4MB with spiffs",
    "// ============================================================",
    "",
    "#include <Wire.h>",
    "#include <SPI.h>",
    "#include <LoRa.h>",
    "#include <Adafruit_BMP280.h>",
    "#include <esp_task_wdt.h>",
    "",
    config, "",
    protocol, "",
    sensor, "",
    flash, "",
    persist, "",
    flight_main, "",
])

# ---------------- ground sketch ----------------
dash = clean(read("src/ground/DashboardHTML.h"))
ground_main = clean(read("src/ground/main.cpp"))

ground_ino = "\n".join([
    "// ============================================================",
    "// ROCKET GROUND STATION - Arduino IDE sketch",
    "// Auto-generated from the PlatformIO sources. Upload to the",
    "// second ESP32 WROOM wired to the SX1278 receiver.",
    "// Board: ESP32 Dev Module. WiFi AP: RocketGND-xxxx / rocket2024",
    "// ============================================================",
    "",
    "#include <SPI.h>",
    "#include <LoRa.h>",
    "#include <WiFi.h>",
    "#include <WebServer.h>",
    "#include <ArduinoJson.h>",
    "",
    config, "",
    protocol, "",
    dash, "",
    ground_main, "",
])

outdir = ROOT / "arduino_ide"
fdir = outdir / "flight_sketch"
gdir = outdir / "ground_sketch"
fdir.mkdir(parents=True, exist_ok=True)
gdir.mkdir(parents=True, exist_ok=True)
(fdir / "flight_sketch.ino").write_text(flight_ino, encoding="utf-8")
(gdir / "ground_sketch.ino").write_text(ground_ino, encoding="utf-8")

# sanity: no local includes may remain
for path in [fdir / "flight_sketch.ino", gdir / "ground_sketch.ino"]:
    body = path.read_text(encoding="utf-8")
    leftover = [l for l in body.splitlines() if l.strip().startswith('#include "')]
    assert not leftover, f"{path}: leftover local includes: {leftover}"
    print(f"{path}  ({len(body.splitlines())} lines)")
print("OK")
