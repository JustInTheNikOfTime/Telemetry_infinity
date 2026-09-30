#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_BMP280.h>
#include <esp_task_wdt.h>
#include "Config.h"
#include "Protocol.h"
#include "SensorFilter.h"
#include "FlashLogger.h"
#include "Persist.h"

#define WDT_TIMEOUT_SECONDS 5

// --- RTC RAM State Retention (survives soft resets; magic-guarded) ---
#define RTC_MAGIC 0xC0FFEE42u
RTC_DATA_ATTR uint32_t rtc_magic = 0;
RTC_DATA_ATTR FlightState rtc_state = STATE_BOOT;
RTC_DATA_ATTR float rtc_launch_pressure = 0.0f;
RTC_DATA_ATTR float rtc_max_altitude = 0.0f;

// --- Global Objects ---
Adafruit_BMP280 bmp;
SensorFilter altFilter(FILTER_EMA_ALPHA);
FlashLogger logger;

// --- State Variables ---
float currentAltitude = 0.0f;
float maxAltitude = 0.0f;
uint16_t packetSequence = 0;
int apogeePersistenceCounter = 0;
int apogeeBurstCounter = 0;
uint32_t lastTelemetryTime = 0;
uint32_t lastLogTime = 0;
uint32_t lastPersistTime = 0;
int landingCounter = 0;
// Serial line assembly (replaces blocking Serial.readStringUntil)
char serialBuf[24];
uint8_t serialLen = 0;

// Endian swap utility
uint16_t swapEndian(uint16_t val) {
    return (val << 8) | (val >> 8);
}

// ---- Serial command polling (non-blocking) ----
// Reads available bytes into a small buffer; returns true with a completed
// line in `out`. Never stalls the flight loop, unlike readStringUntil().
bool pollSerialCommand(char* out, uint8_t maxlen) {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialLen > 0) {
                serialBuf[serialLen] = '\0';
                strncpy(out, serialBuf, maxlen);
                serialLen = 0;
                return true;
            }
        } else if (serialLen < sizeof(serialBuf) - 1) {
            serialBuf[serialLen++] = c;
        }
    }
    return false;
}

void persistFlightState() {
    PersistData d;
    d.launch_pressure = rtc_launch_pressure;
    d.max_altitude = rtc_max_altitude;
    d.state = (uint8_t)rtc_state;
    persistSave(d);
}

void setup() {
    Serial.begin(115200);

    // 80 MHz: this workload doesn't need 240 MHz — saves ~40-50 mA average,
    // nearly doubling endurance on the single-cell battery.
    setCpuFrequencyMhz(80);

    // Setup Watchdog Timer
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);

    // NOTE: no LEDs/buzzer on this flight board — status is observed via
    // telemetry packets and the USB serial console.

    // Battery voltage via external 100k/100k divider on GPIO34 (input-only)
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db); // full ~3.3V range

    // Initialize Flash Logger
    if (!logger.begin()) {
        Serial.println("WARNING: Flash logging disabled.");
    }

    // ONLY wait for DUMP commands if this is a fresh ground boot.
    // If the battery bounced mid-flight, we need to bypass this instantly!
    if (rtc_state == STATE_BOOT || rtc_state == STATE_LANDED || rtc_state == STATE_READY) {
        delay(1000); // Give user time to open terminal
        Serial.println("Send 'DUMP' within 3 seconds to read blackbox data, or 'ERASE' to clear.");
        uint32_t bootTime = millis();
        while (millis() - bootTime < 3000) {
            char cmd[24];
            if (pollSerialCommand(cmd, sizeof(cmd))) {
                if (strcmp(cmd, "DUMP") == 0) {
                    logger.dumpLogToSerial();
                } else if (strcmp(cmd, "ERASE") == 0) {
                    logger.eraseLog();
                }
            }
            esp_task_wdt_reset();
            delay(10);
        }
    }

    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);

    if (!LoRa.begin(LORA_FREQ)) {
        Serial.println("CRITICAL: LoRa init failed. Halting.");
        while(1) {
            Serial.println("LoRa FAULT");   // heartbeat via USB instead of an LED
            delay(1000);
        }
    }

    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setSpreadingFactor(LORA_SPREADFACTOR);
    LoRa.setCodingRate4(LORA_CODINGRATE);
    LoRa.setPreambleLength(LORA_PREAMBLE);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setTxPower(LORA_TX_POWER, PA_OUTPUT_PA_BOOST_PIN);
    LoRa.enableCrc();

    Wire.begin(PIN_BMP_SDA, PIN_BMP_SCL);
    Wire.setTimeOut(100);

    if (!bmp.begin(0x76, BMP280_CHIPID)) {
        if (!bmp.begin(0x77, BMP280_CHIPID)) {
            Serial.println("CRITICAL: BMP280 init failed. Halting.");
            while(1) {
                Serial.println("BMP FAULT");   // heartbeat via USB instead of an LED
                delay(1000);
            }
        }
    }

    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X8,
                    Adafruit_BMP280::FILTER_X4,      // Lower hardware IIR filter to prevent lag in fast rocket
                    Adafruit_BMP280::STANDBY_MS_1);

    // ---- State recovery ----
    // RTC RAM is valid only if the magic word survived (soft reset).
    if (rtc_magic != RTC_MAGIC) {
        // Garbage RTC RAM (cold boot). Try the flash mirror before recalibrating.
        PersistData d;
        if (persistLoad(d) && d.state == STATE_ASCENT) {
            rtc_magic = RTC_MAGIC;
            rtc_state = (FlightState)d.state;
            rtc_launch_pressure = d.launch_pressure;
            rtc_max_altitude = d.max_altitude;
            maxAltitude = rtc_max_altitude;
            Serial.printf("RECOVERED from flash after cold boot! State: %d, P0: %.2f, MaxAlt: %.1f\n",
                          rtc_state, rtc_launch_pressure, rtc_max_altitude);
        } else {
            rtc_magic = RTC_MAGIC;
            rtc_state = STATE_BOOT;
        }
    } else if (rtc_state == STATE_ASCENT) {
        // Soft reset mid-flight: RTC RAM is the fresher copy; mirror it to flash.
        maxAltitude = rtc_max_altitude;
        persistFlightState();
        Serial.printf("Recovered Mid-Flight! State: %d, P0: %.2f\n", rtc_state, rtc_launch_pressure);
    } else {
        maxAltitude = rtc_max_altitude;
    }

    if (rtc_state == STATE_BOOT || rtc_state == STATE_LANDED) {
        Serial.println("Establishing new launch reference...");
        rtc_state = STATE_READY;
        float p_sum = 0;
        for (int i = 0; i < 50; i++) {
            p_sum += bmp.readPressure();
            delay(20);
        }
        rtc_launch_pressure = (p_sum / 50.0f) / 100.0f;
        rtc_max_altitude = 0.0f;
        maxAltitude = 0.0f;
        packetSequence = 0;
        apogeeBurstCounter = 0;
        apogeePersistenceCounter = 0;
        persistFlightState();   // store the new P0 immediately
        Serial.printf("Launch Pressure Set: %.2f hPa\n", rtc_launch_pressure);
    }
}

void transmitTelemetry(uint8_t type, float altitude) {
    TelemetryPacket pkt;
    pkt.rocket_id = ROCKET_ID;
    pkt.type = type;
    pkt.sequence = swapEndian(packetSequence++);

    // Prevent negative altitude noise on the launch pad from underflowing
    // the unsigned 16-bit integer and wrapping around to 6553.5 meters.
    if (altitude < 0.0f) {
        altitude = 0.0f;
    }

    uint16_t alt_encoded = (uint16_t)round(altitude * 10.0f);
    pkt.altitude = swapEndian(alt_encoded);

    LoRa.beginPacket();
    LoRa.write((uint8_t*)&pkt, sizeof(TelemetryPacket));
    LoRa.endPacket(true);
}

void loop() {
    esp_task_wdt_reset();
    uint32_t now = millis();

    // 20 Hz evaluation tick — computed BEFORE the log block updates
    // lastLogTime, so the state machine sees the same tick the logger uses.
    bool tick20 = (now - lastLogTime >= 50);

    float p = bmp.readPressure() / 100.0f;
    float raw_altitude = 44330.0 * (1.0 - pow(p / rtc_launch_pressure, 0.1903));
    currentAltitude = altFilter.update(raw_altitude);

    if (currentAltitude > maxAltitude) {
        maxAltitude = currentAltitude;
        rtc_max_altitude = maxAltitude;
    }

    // High frequency internal logging (approx 20Hz, writing in batches of 10)
    if (tick20) {
        float battV = analogReadMilliVolts(PIN_BATT_ADC) / 1000.0f * BATT_DIVIDER_RATIO;
        logger.log(now, rtc_state, currentAltitude, maxAltitude, battV);
        lastLogTime = now;
    }

    // Periodic flash mirror of flight-critical state while flying
    if ((rtc_state == STATE_ASCENT || rtc_state == STATE_DESCENT || rtc_state == STATE_NEAR_APOGEE)
        && (now - lastPersistTime >= PERSIST_PERIOD_MS)) {
        persistFlightState();
        lastPersistTime = now;
    }

    switch (rtc_state) {
        case STATE_READY:
            if (currentAltitude > LAUNCH_ALT_THRESHOLD) {
                rtc_state = STATE_ASCENT;
                logger.forceFlush();
                persistFlightState();   // lock in P0 + ASCENT at launch
            }
            break;

        case STATE_ASCENT:
            // Apogee persistence counted at the 20 Hz log tick (~500 ms of
            // sustained fall), not raw loop iterations — immune to filter
            // ripple and single-sample glitches.
            if (tick20) {
                if (currentAltitude < maxAltitude - APOGEE_FALL_THRESHOLD) {
                    apogeePersistenceCounter++;
                    if (apogeePersistenceCounter >= APOGEE_PERSISTENCE) {
                        rtc_state = STATE_APOGEE_LOCKED;
                        logger.forceFlush();
                        apogeeBurstCounter = 0;
                    }
                } else {
                    apogeePersistenceCounter = 0;
                }
            }
            break;

        case STATE_APOGEE_LOCKED:
            rtc_state = STATE_DESCENT;
            landingCounter = 0;
            break;

        case STATE_DESCENT:
            // Landing requires persistence: 50 consecutive ticks (~2.5 s)
            // below threshold, so a tree canopy or hover does not instantly
            // end the flight in the log.
            if (tick20) {
                if (currentAltitude < 5.0f) {
                    landingCounter++;
                    if (landingCounter >= 50) {
                        rtc_state = STATE_LANDED;
                        logger.forceFlush();
                        persistFlightState();
                    }
                } else {
                    landingCounter = 0;
                }
            }
            break;
    }

    if (rtc_state == STATE_APOGEE_LOCKED || (apogeeBurstCounter > 0 && apogeeBurstCounter < APOGEE_BURST_COUNT)) {
        if (now - lastTelemetryTime >= APOGEE_BURST_INTERVAL) {
            transmitTelemetry(PKT_TYPE_APOGEE, maxAltitude);
            lastTelemetryTime = now;
            apogeeBurstCounter++;
        }
    } else {
        if (now - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
            transmitTelemetry(PKT_TYPE_NORMAL_ALTITUDE, currentAltitude);
            lastTelemetryTime = now;
        }
    }

    // Non-blocking serial command poll (replaces readStringUntil)
    char cmd[24];
    if (pollSerialCommand(cmd, sizeof(cmd))) {
        if (strcmp(cmd, "DUMP") == 0) {
            logger.forceFlush();
            logger.dumpLogToSerial();
        }
    }

    delay(10);
}
