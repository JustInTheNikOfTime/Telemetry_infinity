// ============================================================
// ROCKET GROUND STATION - Arduino IDE sketch
// Auto-generated from the PlatformIO sources. Upload to the
// second ESP32 WROOM wired to the SX1278 receiver.
// Board: ESP32 Dev Module. WiFi AP: RocketGND-xxxx / rocket2024
// ============================================================

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>


// --- Hardware Pins (Standardized per spec) ---
#define PIN_BMP_SDA  21
#define PIN_BMP_SCL  22

#define PIN_LORA_MISO 19
#define PIN_LORA_MOSI 23
#define PIN_LORA_SCK  18
#define PIN_LORA_NSS  5
#define PIN_LORA_RST  14
#define PIN_LORA_DIO0 26

#define PIN_LED_SENS   27
#define PIN_LED_TX     25
#define PIN_LED_STATUS 33
#define PIN_BUZZER     4

// --- LoRa Configuration (FAST TELEMETRY MODE) ---
// SF7 = shortest packets & most Doppler-tolerant (right choice for rockets;
// not max-range, but range is far beyond model-rocket needs).
#define LORA_FREQ         434.5E6 // 434.500 MHz
#define LORA_BANDWIDTH    125E3   // 125 kHz
#define LORA_SPREADFACTOR 7       // SF7 (Fastest transmission)
#define LORA_CODINGRATE   8       // 4/8 Maximum Error Correction to recover corrupted packets
#define LORA_PREAMBLE     8
#define LORA_SYNC_WORD    0x12    // Standard LoRa Sync Word
#define LORA_TX_POWER     13      // 13 dBm: license-free in most regions, ~30% less TX current, ample link margin

// --- Battery Monitoring (hardware: 100k/100k divider VBAT -> GPIO34) ---
#define PIN_BATT_ADC        34    // input-only ADC pin, safe for divider tap
#define BATT_DIVIDER_RATIO  2.0f  // (Rtop + Rbottom) / Rbottom for 100k/100k

// --- Flight Dynamics & Filtering Config ---
// Barometric moving average weight (0.0 to 1.0). Lower = more smoothing, higher = less lag.
// For a fast moving rocket, we want less software lag!
#define FILTER_EMA_ALPHA       0.4f
#define MEDIAN_WINDOW_SIZE     5

// Launch Detection (TBD in spec, establishing baseline)
#define LAUNCH_ALT_THRESHOLD   10.0f  // meters above P0 to declare launch
#define LAUNCH_ACCEL_THRESHOLD 15.0f  // m/s estimated velocity to confirm launch

// Apogee Detection
// Require X consecutive falling samples before declaring apogee to prevent false positives from noise.
#define APOGEE_FALL_THRESHOLD  2.0f   // meters below max altitude
#define APOGEE_PERSISTENCE     10     // samples

// Telemetry Timing
// 10 Hz = 100 ms per packet (safer overhead for SF7 125kHz ToA)
#define TELEMETRY_INTERVAL_MS  100     // 10Hz telemetry interval
#define APOGEE_BURST_INTERVAL  100     // 10Hz
#define APOGEE_BURST_COUNT     10     // 10 packets for apogee lock mark

// IDs
#define ROCKET_ID              37     // Standard test ID

#include <stdint.h>

// Packet Types
#define PKT_TYPE_NORMAL_ALTITUDE 0x01
#define PKT_TYPE_APOGEE          0x02

// Exact 6-byte payload specified in the reference document.
// Using compiler attributes to prevent padding bytes.
#pragma pack(push, 1)
struct TelemetryPacket {
    uint8_t  rocket_id;
    uint8_t  type;
    uint16_t sequence;   // Stored in Big-Endian format per spec!
    uint16_t altitude;   // Stored in Big-Endian format, 0.1m resolution
};
#pragma pack(pop)

// Flight States
enum FlightState {
    STATE_BOOT = 0,
    STATE_READY,
    STATE_ASCENT,
    STATE_NEAR_APOGEE,
    STATE_APOGEE_LOCKED,
    STATE_DESCENT,
    STATE_LANDED
};

// Auto-generated from DashboardHTML.h
// This is the full dashboard HTML served by the Ground Station ESP32 in AP mode.
// Stored in PROGMEM to avoid consuming precious RAM.
#include <pgmspace.h>

const char DASHBOARD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Rocket Ground Station</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: 'Segoe UI', monospace;
    background: #0d1117;
    color: #c9d1d9;
    min-height: 100vh;
    padding: 16px;
  }
  h1 {
    color: #58a6ff;
    font-size: 1.4rem;
    letter-spacing: 2px;
    text-transform: uppercase;
    border-bottom: 1px solid #21262d;
    padding-bottom: 10px;
    margin-bottom: 16px;
  }
  h1 span { color: #3fb950; font-size: 0.8rem; margin-left: 12px; }
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 12px;
    margin-bottom: 16px;
  }
  @media(max-width: 700px) { .grid { grid-template-columns: 1fr; } }
  .card {
    background: #161b22;
    border: 1px solid #21262d;
    border-radius: 8px;
    padding: 14px 18px;
  }
  .card label {
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 1px;
    color: #8b949e;
    display: block;
    margin-bottom: 4px;
  }
  .card .val {
    font-size: 2rem;
    font-weight: bold;
    color: #f0f6fc;
  }
  .card .val.apogee { color: #f85149; }
  .card .val.good   { color: #3fb950; }
  .card .val.warn   { color: #d29922; }
  .card .unit { font-size: 0.85rem; color: #8b949e; margin-left: 4px; }

  /* RSSI Bar */
  .rssi-bar-wrap {
    background: #0d1117;
    border-radius: 4px;
    height: 18px;
    width: 100%;
    margin-top: 8px;
    overflow: hidden;
  }
  .rssi-bar {
    height: 100%;
    border-radius: 4px;
    transition: width 0.5s ease, background 0.5s ease;
    width: 0%;
    background: #3fb950;
  }
  .rssi-label { font-size: 0.7rem; color: #8b949e; margin-top: 4px; }

  /* Chart */
  .chart-card { background: #161b22; border: 1px solid #21262d; border-radius: 8px; padding: 14px 18px; margin-bottom: 16px; }
  .chart-card label { font-size: 0.7rem; text-transform: uppercase; letter-spacing: 1px; color: #8b949e; display: block; margin-bottom: 8px; }
  canvas#altChart { width: 100% !important; height: 200px !important; }

  /* Table */
  .table-card { background: #161b22; border: 1px solid #21262d; border-radius: 8px; padding: 14px 18px; }
  .table-card label { font-size: 0.7rem; text-transform: uppercase; letter-spacing: 1px; color: #8b949e; display: block; margin-bottom: 8px; }
  table { width: 100%; border-collapse: collapse; font-size: 0.82rem; }
  th { color: #8b949e; border-bottom: 1px solid #21262d; padding: 6px 8px; text-align: left; font-weight: 500; }
  td { padding: 5px 8px; border-bottom: 1px solid #161b22; }
  tr:hover td { background: #21262d; }
  .badge {
    display: inline-block;
    padding: 2px 8px;
    border-radius: 12px;
    font-size: 0.7rem;
    font-weight: bold;
  }
  .badge.normal { background: #1f3a1f; color: #3fb950; }
  .badge.apogee { background: #3a1f1f; color: #f85149; }
  .dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block; background: #3fb950; animation: blink 1s infinite; }
  @keyframes blink { 0%,100%{opacity:1} 50%{opacity:0.2} }

  /* Status bar */
  .statusbar { font-size: 0.72rem; color: #8b949e; text-align: right; margin-top: 12px; }
</style>
</head>
<body>

<h1>&#x1F680; Rocket Ground Station <span><span class="dot"></span> LIVE</span></h1>

<div class="grid">
  <div class="card">
    <label>Latest Altitude</label>
    <div><span class="val" id="latestAlt">--</span><span class="unit">m</span></div>
  </div>
  <div class="card">
    <label>Max Altitude (Apogee)</label>
    <div><span class="val apogee" id="maxAlt">--</span><span class="unit">m</span></div>
  </div>
  <div class="card">
    <label>Packets Received</label>
    <div><span class="val good" id="pktCount">0</span></div>
  </div>
  <div class="card">
    <label>RSSI (Signal Strength)</label>
    <div><span class="val" id="rssiVal">--</span><span class="unit">dBm</span></div>
    <div class="rssi-bar-wrap"><div class="rssi-bar" id="rssiBar"></div></div>
    <div class="rssi-label" id="rssiLabel">Waiting...</div>
  </div>
  <div class="card">
    <label>SNR</label>
    <div><span class="val" id="snrVal">--</span><span class="unit">dB</span></div>
  </div>
  <div class="card">
    <label>Last Rocket ID</label>
    <div><span class="val" id="rocketId">--</span></div>
  </div>
</div>

<div class="chart-card">
  <label>Altitude Profile</label>
  <canvas id="altChart"></canvas>
</div>

<div class="table-card">
  <label>Packet Log</label>
  <table>
    <thead><tr>
      <th>Time</th><th>ID</th><th>Type</th><th>Seq</th><th>Alt (m)</th><th>RSSI</th><th>SNR</th>
    </tr></thead>
    <tbody id="pktTable"></tbody>
  </table>
</div>

<div class="statusbar" id="statusBar">Connecting...</div>

<script>
// ---- Chart Setup ----
const canvas = document.getElementById('altChart');
const ctx = canvas.getContext('2d');
const MAX_POINTS = 120;
let altHistory = [];
let maxAltSeen = 0;
let totalPackets = 0;
let lastSeq = -1;

function drawChart() {
  const W = canvas.offsetWidth;
  const H = canvas.offsetHeight;
  canvas.width = W;
  canvas.height = H;

  ctx.clearRect(0, 0, W, H);

  // Grid
  ctx.strokeStyle = '#21262d';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = H - (i / 4) * H;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
  }

  if (altHistory.length < 2) return;

  const maxVal = Math.max(...altHistory, 10);
  const minVal = 0;
  const range = maxVal - minVal || 1;

  // Fill gradient
  const grad = ctx.createLinearGradient(0, 0, 0, H);
  grad.addColorStop(0, 'rgba(88,166,255,0.25)');
  grad.addColorStop(1, 'rgba(88,166,255,0)');

  ctx.beginPath();
  ctx.moveTo(0, H);
  altHistory.forEach((v, i) => {
    const x = (i / (MAX_POINTS - 1)) * W;
    const y = H - ((v - minVal) / range) * H;
    i === 0 ? ctx.lineTo(x, y) : ctx.lineTo(x, y);
  });
  ctx.lineTo(W, H);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // Line
  ctx.beginPath();
  ctx.strokeStyle = '#58a6ff';
  ctx.lineWidth = 2;
  altHistory.forEach((v, i) => {
    const x = (i / (MAX_POINTS - 1)) * W;
    const y = H - ((v - minVal) / range) * H;
    i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Apogee line
  if (maxAltSeen > 0) {
    const ay = H - ((maxAltSeen - minVal) / range) * H;
    ctx.beginPath();
    ctx.strokeStyle = '#f85149';
    ctx.setLineDash([4, 4]);
    ctx.lineWidth = 1;
    ctx.moveTo(0, ay); ctx.lineTo(W, ay);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = '#f85149';
    ctx.font = '10px monospace';
    ctx.fillText('APOGEE ' + maxAltSeen.toFixed(1) + 'm', 6, ay - 4);
  }
}

// ---- RSSI Bar ----
function updateRSSI(rssi) {
  // RSSI typically -120 (terrible) to -40 (excellent)
  const clamped = Math.max(-120, Math.min(-40, rssi));
  const pct = ((clamped + 120) / 80) * 100;
  const bar = document.getElementById('rssiBar');
  bar.style.width = pct + '%';
  if (pct > 65)      { bar.style.background = '#3fb950'; }
  else if (pct > 35) { bar.style.background = '#d29922'; }
  else               { bar.style.background = '#f85149'; }

  let label = 'Poor';
  if (pct > 65) label = 'Good';
  else if (pct > 35) label = 'Fair';
  document.getElementById('rssiLabel').textContent = label + ' (' + pct.toFixed(0) + '%)';
}

// ---- Packet Poll ----
async function fetchPackets() {
  try {
    const res = await fetch('/api/packets');
    if (!res.ok) return;
    const packets = await res.json();
    if (!packets || packets.length === 0) return;

    const latest = packets[packets.length - 1];

    // Stats
    document.getElementById('latestAlt').textContent = latest.alt.toFixed(1);
    document.getElementById('rssiVal').textContent = latest.rssi;
    document.getElementById('snrVal').textContent = latest.snr.toFixed(1);
    document.getElementById('rocketId').textContent = latest.rocket_id;
    updateRSSI(latest.rssi);

    // Count only genuinely new packets
    packets.forEach(p => {
      if (p.seq !== lastSeq) {
        totalPackets++;
        lastSeq = p.seq;
        if (p.alt > maxAltSeen) maxAltSeen = p.alt;
        altHistory.push(p.alt);
        if (altHistory.length > MAX_POINTS) altHistory.shift();
      }
    });

    document.getElementById('pktCount').textContent = totalPackets;
    document.getElementById('maxAlt').textContent = maxAltSeen.toFixed(1);

    // Table (show latest 25 in reverse)
    const tbody = document.getElementById('pktTable');
    tbody.innerHTML = '';
    const recent = [...packets].reverse().slice(0, 25);
    recent.forEach(p => {
      const tr = document.createElement('tr');
      const typeLabel = p.type === 2
        ? '<span class="badge apogee">APOGEE</span>'
        : '<span class="badge normal">NORMAL</span>';
      tr.innerHTML = `<td>${p.ts}</td><td>${p.rocket_id}</td><td>${typeLabel}</td>
        <td>${p.seq}</td><td><b>${p.alt.toFixed(1)}</b></td>
        <td>${p.rssi} dBm</td><td>${p.snr.toFixed(1)} dB</td>`;
      tbody.appendChild(tr);
    });

    drawChart();
    document.getElementById('statusBar').textContent =
      'Last update: ' + new Date().toLocaleTimeString() + ' | Polling /api/packets';

  } catch(e) {
    document.getElementById('statusBar').textContent = 'Connection error: ' + e.message;
  }
}

setInterval(fetchPackets, 500);
setInterval(drawChart, 1000);
fetchPackets();
</script>
</body>
</html>
)rawhtml";

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// --- WiFi AP Credentials ---
#define AP_SSID_PREFIX "RocketGND"
#define AP_PASSWORD    "rocket2024"
#define AP_CHANNEL     6
#define AP_MAX_CLIENTS 4

// --- Packet Ring Buffer ---
#define RING_BUFFER_SIZE 50

struct ReceivedPacket {
    uint8_t  rocket_id;
    uint8_t  type;
    uint16_t seq;
    float    alt;
    int      rssi;
    float    snr;
    char     ts[12]; // HH:MM:SS
};

ReceivedPacket ringBuffer[RING_BUFFER_SIZE];
uint16_t ringHead = 0;
uint16_t ringCount = 0;
portMUX_TYPE ringMux = portMUX_INITIALIZER_UNLOCKED;

// --- Web Server ---
WebServer server(80);

// Endian swap
uint16_t swapEndian(uint16_t val) {
    return (val << 8) | (val >> 8);
}

// Get current time as HH:MM:SS string (seconds since boot, formatted)
void getTimeStr(char* buf) {
    uint32_t s = millis() / 1000;
    uint32_t h = s / 3600;
    uint32_t m = (s % 3600) / 60;
    uint32_t sec = s % 60;
    snprintf(buf, 12, "%02lu:%02lu:%02lu", h, m, sec);
}

// ---- Web Server Handlers ----
void handleRoot() {
    server.send_P(200, "text/html", DASHBOARD_HTML);
}

void handlePackets() {
    // Build JSON array from ring buffer — ArduinoJson v7 API
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    portENTER_CRITICAL(&ringMux);
    uint16_t count = ringCount;
    uint16_t head = ringHead;
    portEXIT_CRITICAL(&ringMux);

    // Iterate in chronological order
    uint16_t start = (count < RING_BUFFER_SIZE) ? 0 : head;
    for (uint16_t i = 0; i < count; i++) {
        uint16_t idx = (start + i) % RING_BUFFER_SIZE;
        JsonObject obj = arr.add<JsonObject>();
        obj["rocket_id"] = ringBuffer[idx].rocket_id;
        obj["type"]      = ringBuffer[idx].type;
        obj["seq"]       = ringBuffer[idx].seq;
        obj["alt"]       = ringBuffer[idx].alt;
        obj["rssi"]      = ringBuffer[idx].rssi;
        obj["snr"]       = ringBuffer[idx].snr;
        obj["ts"]        = ringBuffer[idx].ts;
    }

    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

// ---- LoRa Receive Task (runs on Core 0) ----
void loraTask(void* pvParams) {
    while (true) {
        int packetSize = LoRa.parsePacket();
        if (packetSize == sizeof(TelemetryPacket)) {
            TelemetryPacket pkt;
            LoRa.readBytes((uint8_t*)&pkt, sizeof(TelemetryPacket));

            uint16_t seq = swapEndian(pkt.sequence);
            uint16_t alt_encoded = swapEndian(pkt.altitude);
            float    decoded_alt = (float)alt_encoded / 10.0f;
            int      rssi = LoRa.packetRssi();
            float    snr  = LoRa.packetSnr();

            // Store in ring buffer (thread safe)
            portENTER_CRITICAL(&ringMux);
            ReceivedPacket& slot = ringBuffer[ringHead];
            slot.rocket_id = pkt.rocket_id;
            slot.type      = pkt.type;
            slot.seq       = seq;
            slot.alt       = decoded_alt;
            slot.rssi      = rssi;
            slot.snr       = snr;
            getTimeStr(slot.ts);
            ringHead = (ringHead + 1) % RING_BUFFER_SIZE;
            if (ringCount < RING_BUFFER_SIZE) ringCount++;
            portEXIT_CRITICAL(&ringMux);

            // Also output JSON to USB Serial for ground_logger.py
            Serial.printf("{\"rocket_id\":%d,\"type\":%d,\"seq\":%d,\"alt\":%.1f,\"rssi\":%d,\"snr\":%.2f}\n",
                pkt.rocket_id, pkt.type, seq, decoded_alt, rssi, snr);

            // Flash RX LED and Beep Buzzer
            digitalWrite(PIN_LED_TX, HIGH);
            digitalWrite(PIN_BUZZER, HIGH);
            delay(30);
            digitalWrite(PIN_LED_TX, LOW);
            digitalWrite(PIN_BUZZER, LOW);

        } else if (packetSize > 0) {
            Serial.printf("{\"warning\":\"Unknown packet size: %d bytes\"}\n", packetSize);
        }

        vTaskDelay(1); // Yield to RTOS
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_LED_TX, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); // Ensure it starts quiet

    // ---- Start WiFi AP ----
    // Append last 2 bytes of MAC to SSID for uniqueness
    uint8_t mac[6];

    // Explicit AP mode + static IP config.
    // Clean up any old corrupted state from NVS first:
    WiFi.disconnect(true, true);
    WiFi.softAPdisconnect(true);
    delay(100);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPmacAddress(mac); // Use AP MAC specifically

    IPAddress apIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, gateway, subnet);

    char ssid[24];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);

    // ssid, password, channel, hidden, max_connections
    WiFi.softAP(ssid, AP_PASSWORD, AP_CHANNEL, 0, AP_MAX_CLIENTS);

    // Must wait for AP to settle before reading IP or starting server —
    // skipping this is the #1 cause of "access denied" on Android/Windows.
    delay(500);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("{\"log\":\"AP Started. SSID: %s | Password: %s | Dashboard: http://%s\"}\n",
                  ssid, AP_PASSWORD, ip.toString().c_str());

    // ---- Start Web Server ----
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/packets", HTTP_GET, handlePackets);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial.printf("{\"log\":\"Web Dashboard: http://%s\"}\n", ip.toString().c_str());

    // ---- Start LoRa ----
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);

    bool loraOk = LoRa.begin(LORA_FREQ);
    if (!loraOk) {
        Serial.println("{\"error\":\"CRITICAL: LoRa init failed. Check wiring!\"}");
        // Do not use while(1) here! We want the Web Server to keep running
        // so the user can still connect and we don't get 'Site can't be reached'.
    } else {
        LoRa.setSignalBandwidth(LORA_BANDWIDTH);
        LoRa.setSpreadingFactor(LORA_SPREADFACTOR);
        LoRa.setCodingRate4(LORA_CODINGRATE);
        LoRa.setPreambleLength(LORA_PREAMBLE);
        LoRa.setSyncWord(LORA_SYNC_WORD);
        LoRa.enableCrc();
        Serial.printf("{\"log\":\"LoRa Ready. Listening on 434.500 MHz\"}\n");

        // ---- Pin LoRa receive task to Core 0 ----
        xTaskCreatePinnedToCore(loraTask, "LoRaRX", 4096, NULL, 2, NULL, 0);
    }

    digitalWrite(PIN_LED_STATUS, HIGH);
}

void loop() {
    // Core 1 handles HTTP clients
    server.handleClient();
    delay(1);
}
