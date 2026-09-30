#pragma once
#include <Arduino.h>
#include <LittleFS.h>

// Flash mirror of flight-critical state. RTC RAM (guarded by a magic word)
// is the primary carrier across resets; this file is the fallback for a
// cold boot (battery fully removed / brownout), where RTC RAM is garbage.
// Without it, altitude after a mid-flight power cut would silently
// recalibrate to ~0 m at whatever pressure the rocket happened to fall in.

#define PERSIST_FILENAME   "/persist.bin"
#define PERSIST_MAGIC      0x50525331u   // "PRS1"
#define PERSIST_PERIOD_MS  5000u         // refresh rate while flying

#pragma pack(push, 1)
struct PersistData {
    uint32_t magic;
    float    launch_pressure;
    float    max_altitude;
    uint8_t  state;
    uint8_t  checksum;   // byte-sum of all preceding bytes
};
#pragma pack(pop)

uint8_t persistChecksum(const PersistData& d) {
    const uint8_t* p = (const uint8_t*)&d;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < sizeof(PersistData) - 1; i++) sum += p[i];
    return sum;
}

bool persistLoad(PersistData& out) {
    if (!LittleFS.exists(PERSIST_FILENAME)) return false;
    File f = LittleFS.open(PERSIST_FILENAME, FILE_READ);
    if (!f) return false;
    if (f.size() != sizeof(PersistData)) { f.close(); return false; }
    PersistData d;
    bool ok = (f.read((uint8_t*)&d, sizeof(d)) == sizeof(d));
    f.close();
    if (!ok || d.magic != PERSIST_MAGIC || d.checksum != persistChecksum(d)) return false;
    out = d;
    return true;
}

bool persistSave(const PersistData& d) {
    PersistData tmp = d;
    tmp.magic = PERSIST_MAGIC;
    tmp.checksum = persistChecksum(tmp);
    File f = LittleFS.open(PERSIST_FILENAME, FILE_WRITE);
    if (!f) return false;
    bool ok = (f.write((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp));
    f.close();
    return ok;
}
