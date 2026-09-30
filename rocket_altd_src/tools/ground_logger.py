#!/usr/bin/env python3
"""
ground_logger.py — Rocket Ground Station Serial Logger
Reads JSON telemetry from the Ground Station ESP32 over USB-Serial,
displays it live, and saves everything to a timestamped CSV.

Usage:
    python3 tools/ground_logger.py              # auto-detect port
    python3 tools/ground_logger.py /dev/ttyUSB1 # force port
"""

import sys
import glob
import time
import json
import csv
import serial
import serial.tools.list_ports
from datetime import datetime

BAUD_RATE = 115200


# ── Colours ──────────────────────────────────────────────────────────────────
class C:
    RESET  = "\033[0m"
    CYAN   = "\033[96m"
    GREEN  = "\033[92m"
    YELLOW = "\033[93m"
    RED    = "\033[91m"
    BOLD   = "\033[1m"
    DIM    = "\033[2m"


# ── Port Auto-Detection ───────────────────────────────────────────────────────
def find_esp32_ports():
    """Return a list of likely ESP32 serial port paths."""
    candidates = []

    # Use pyserial's list_ports for cross-platform support
    for port in serial.tools.list_ports.comports():
        desc = (port.description or "").lower()
        mfr  = (port.manufacturer or "").lower()
        # Common ESP32 USB bridge chips
        if any(k in desc or k in mfr for k in [
            "cp210", "ch340", "ch341", "ftdi", "silabs",
            "usb serial", "uart", "esp32"
        ]):
            candidates.append(port.device)

    # Fallback: glob patterns on Linux/macOS
    if not candidates:
        for pattern in ["/dev/ttyUSB*", "/dev/ttyACM*",
                        "/dev/cu.SLAB_USBtoUART*", "/dev/cu.usbserial*"]:
            candidates.extend(glob.glob(pattern))

    return sorted(set(candidates))


def select_port(forced=None):
    if forced:
        print(f"{C.GREEN}Using forced port: {forced}{C.RESET}")
        return forced

    ports = find_esp32_ports()

    if not ports:
        print(f"{C.RED}ERROR: No ESP32 serial ports found. Is the device plugged in?{C.RESET}")
        sys.exit(1)

    if len(ports) == 1:
        print(f"{C.GREEN}Auto-detected port: {ports[0]}{C.RESET}")
        return ports[0]

    # Multiple candidates — let user choose
    print(f"{C.YELLOW}Multiple serial ports found:{C.RESET}")
    for i, p in enumerate(ports):
        print(f"  [{i+1}] {p}")
    while True:
        try:
            choice = int(input("Select port number: ")) - 1
            if 0 <= choice < len(ports):
                return ports[choice]
        except (ValueError, KeyboardInterrupt):
            pass
        print("Invalid selection, try again.")


# ── RSSI Bar ─────────────────────────────────────────────────────────────────
def rssi_bar(rssi: int, width: int = 20) -> str:
    """Returns a coloured ASCII bar representing signal strength."""
    # -120 dBm (terrible) .. -40 dBm (excellent)
    pct = max(0, min(1, (rssi + 120) / 80))
    filled = int(pct * width)
    bar = "█" * filled + "░" * (width - filled)
    if pct > 0.65:
        color = C.GREEN
    elif pct > 0.35:
        color = C.YELLOW
    else:
        color = C.RED
    return f"{color}[{bar}]{C.RESET} {rssi} dBm"


# ── Main ─────────────────────────────────────────────────────────────────────
def main():
    forced_port = sys.argv[1] if len(sys.argv) > 1 else None
    port = select_port(forced_port)

    log_file = f"flight_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    print(f"{C.CYAN}{C.BOLD}Ground Station Logger{C.RESET}")
    print(f"{C.DIM}Logging to: {log_file}{C.RESET}\n")

    # Initialise CSV
    with open(log_file, "w", newline="") as f:
        csv.writer(f).writerow(
            ["Timestamp", "RocketID", "Type", "Sequence", "Altitude_m", "RSSI_dBm", "SNR_dB"]
        )

    ser = None
    max_alt = 0.0

    while True:
        # (Re)connect
        if ser is None or not ser.is_open:
            try:
                ser = serial.Serial(port, BAUD_RATE, timeout=1)
                print(f"{C.GREEN}Connected to {port}{C.RESET}")
            except serial.SerialException as e:
                print(f"{C.RED}Cannot open {port}: {e}. Retrying in 2 s...{C.RESET}")
                time.sleep(2)
                continue

        try:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            try:
                data = json.loads(line)
            except json.JSONDecodeError:
                print(f"{C.DIM}[RAW] {line}{C.RESET}")
                continue

            # System messages from the ESP32
            if "log" in data:
                print(f"{C.CYAN}[*] {data['log']}{C.RESET}")
                continue
            if "error" in data:
                print(f"{C.RED}[!] {data['error']}{C.RESET}")
                continue
            if "warning" in data:
                print(f"{C.YELLOW}[W] {data['warning']}{C.RESET}")
                continue

            # Telemetry packet
            if "rocket_id" not in data:
                continue

            ts     = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            r_id   = data["rocket_id"]
            ptype  = data["type"]
            seq    = data["seq"]
            alt    = float(data["alt"])
            rssi   = int(data["rssi"])
            snr    = float(data["snr"])

            type_str = f"{C.RED}APOGEE{C.RESET}" if ptype == 2 else f"{C.GREEN}NORMAL{C.RESET}"

            if alt > max_alt:
                max_alt = alt

            # Pretty print
            bar = rssi_bar(rssi)
            print(
                f"[{C.DIM}{ts}{C.RESET}] "
                f"ID:{C.BOLD}{r_id:3d}{C.RESET} | "
                f"{type_str} | "
                f"Seq:{seq:05d} | "
                f"Alt: {C.BOLD}{alt:7.1f} m{C.RESET} | "
                f"MaxAlt: {C.YELLOW}{max_alt:7.1f} m{C.RESET} | "
                f"SNR: {snr:5.1f} dB | "
                f"RSSI: {bar}"
            )

            # Append to CSV immediately
            with open(log_file, "a", newline="") as f:
                csv.writer(f).writerow(
                    [ts, r_id, "APOGEE" if ptype == 2 else "NORMAL", seq, alt, rssi, snr]
                )

        except serial.SerialException as e:
            print(f"{C.RED}Serial error: {e}. Reconnecting...{C.RESET}")
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)

        except KeyboardInterrupt:
            print(f"\n{C.CYAN}Shutting down. Log saved to {log_file}{C.RESET}")
            if ser and ser.is_open:
                ser.close()
            break


if __name__ == "__main__":
    main()
