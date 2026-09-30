#!/usr/bin/env python3
"""Render avionics visualizations (bay layout, wiring, power tree, checklist) as PNGs."""
from PIL import Image, ImageDraw, ImageFont

OUT = "visualizations"
import os
os.makedirs(OUT, exist_ok=True)

# ---------- palette ----------
BG      = (13, 17, 23)
PANEL   = (22, 27, 34)
BORDER  = (48, 54, 61)
TXT     = (201, 209, 217)
DIM     = (139, 148, 158)
BLUE    = (88, 166, 255)
GREEN   = (63, 185, 80)
RED     = (248, 81, 73)
YELLOW  = (210, 153, 34)
ORANGE  = (255, 166, 87)
PURPLE  = (188, 140, 255)

def font(size, bold=True):
    for name in (["arialbd.ttf", "arial.ttf"] if bold else ["arial.ttf"]):
        try: return ImageFont.truetype(name, size)
        except: pass
    return ImageFont.load_default()

def new(w, h, title):
    img = Image.new("RGB", (w, h), BG)
    d = ImageDraw.Draw(img)
    d.text((24, 16), title, fill=BLUE, font=font(26))
    d.line([(24, 54), (w-24, 54)], fill=BORDER, width=2)
    return img, d

def box(d, xy, label, color, sub=None, fill=PANEL, tsize=16):
    x0, y0, x1, y1 = xy
    d.rounded_rectangle(xy, radius=8, fill=fill, outline=color, width=2)
    cx = (x0+x1)/2
    if sub:
        d.text((cx, (y0+y1)/2-14), label, fill=TXT, font=font(tsize), anchor="mm")
        d.text((cx, (y0+y1)/2+12), sub, fill=DIM, font=font(12), anchor="mm")
    else:
        d.text((cx, (y0+y1)/2), label, fill=TXT, font=font(tsize), anchor="mm")

def line(d, p0, p1, color, w=3):
    d.line([p0, p1], fill=color, width=w)

def arrow(d, p0, p1, color, w=3):
    line(d, p0, p1, color, w)
    import math
    ang = math.atan2(p1[1]-p0[1], p1[0]-p0[0])
    L = 10
    for da in (2.6, -2.6):
        d.line([p1, (p1[0]-L*math.cos(ang-da), p1[1]-L*math.sin(ang-da))], fill=color, width=w)

# ============================================================
# 1. AVIONICS BAY LAYOUT (cardboard tube, side cutaway)
# ============================================================
W, H = 1100, 1500
img, d = new(W, H, "AVIONICS BAY - cardboard tube, side cutaway")
# tube
d.rounded_rectangle([420, 90, 680, 1400], radius=40, fill=(60, 45, 30), outline=(150, 110, 70), width=5)
# nose arrow
d.polygon([(550, 40), (510, 95), (590, 95)], fill=DIM)
d.text((760, 60), "to nose cone", fill=DIM, font=font(16))
# antenna whip
line(d, (400, 140), (400, 420), GREEN, 5)
d.text((120, 250), "17.3 cm whip\n(vertical, taped,\nopposite battery)", fill=GREEN, font=font(15))
# bulkheads
for y in (150, 1340):
    line(d, (425, y), (675, y), (170, 130, 90), 8)
d.text((760, 145), "plywood bulkhead", fill=DIM, font=font(14))
# modules inside tube
box(d, [455, 200, 645, 320], "Ra-02 + ESP32", BLUE, "perfboard section")
box(d, [470, 360, 630, 470], "BMP280", PURPLE, "dark, vented")
box(d, [470, 500, 630, 610], "buck-boost", YELLOW, "470uF at Ra-02 pins")
# static ports
for i, y in enumerate(range(560, 700, 36)):
    x = 425 if i % 2 == 0 else 662
    d.ellipse([x-6, y-6, x+6, y+6], outline=GREEN, width=3)
d.text((760, 610), "4x 3 mm static ports\n90 deg apart, upper third\n(4-5 tube dia below nose)", fill=GREEN, font=font(15))
box(d, [470, 760, 630, 860], "foam pad", DIM, tsize=14)
# battery
box(d, [470, 900, 630, 1010], "battery", ORANGE, "strapped low")
d.text((760, 940), "Li-ion strapped to wall,\naway from whip", fill=ORANGE, font=font(15))
box(d, [470, 1060, 630, 1160], "TP4056 + switch", RED, "behind hatch")
d.text((760, 1100), "access hatch:\narm + charge w/o opening bay", fill=RED, font=font(15))
# flight path note
d.rounded_rectangle([120, 1180, 380, 1330], radius=10, fill=PANEL, outline=BORDER, width=2)
d.text((250, 1200), "RADIO RULES", fill=BLUE, font=font(16), anchor="mm")
for i, t in enumerate(["cardboard = RF invisible", "whip straight, never coiled", "no carbon fiber anywhere", "IPEX joint hot-glued"]):
    d.text((140, 1235+i*24), t, fill=TXT, font=font(14))
img.save(f"{OUT}/1_avionics_bay.png")

# ============================================================
# 2. WIRING MAP
# ============================================================
W, H = 1400, 950
img, d = new(W, H, "WIRING MAP - flight board (matches firmware pin map)")
# ESP32 center
box(d, [560, 380, 840, 720], "ESP32 DevKit", BLUE, "80 MHz", fill=(25, 40, 65))
esp_pins_L = {"D21": (560, 470), "D22": (560, 510), "D19": (560, 550), "D5": (560, 590)}
esp_pins_R = {"D18": (840, 470), "D23": (840, 510), "D14": (840, 550), "D26": (840, 590)}
# label pins
for p, xy in {**esp_pins_L, **esp_pins_R}.items():
    dx = -22 if xy[0] == 560 else 22
    d.text((xy[0]+dx, xy[1]), p, fill=BLUE, font=font(14), anchor="rm" if dx < 0 else "lm")
# Ra-02 top right
box(d, [1020, 120, 1260, 320], "Ra-02 SX1278", GREEN, "ANT BEFORE POWER")
# BMP top left
box(d, [140, 120, 380, 320], "BMP280", PURPLE, "addr 0x76/0x77")
# battery bottom left
box(d, [100, 700, 330, 840], "TP4056 -> switch", RED, "battery path")
# buck boost bottom center
box(d, [600, 760, 810, 880], "buck-boost", YELLOW, "3.3 V fixed")
# battery divider right bottom
box(d, [1080, 700, 1330, 840], "divider 100k/100k", ORANGE, "junction -> D34")
# wires bmp
for pin, xy in {"SDA": esp_pins_L["D21"], "SCL": esp_pins_L["D22"]}.items():
    line(d, (260, 320), (xy[0]-30, xy[1]), PURPLE, 3)
    d.text(((260+xy[0])/2-30, (320+xy[1])/2), pin, fill=PURPLE, font=font(13))
# wires lora
for pin, xy in {"NSS": esp_pins_L["D5"], "MISO": esp_pins_L["D19"]}.items():
    line(d, (1020, 200), (xy[0]-30, xy[1]), GREEN, 3)
for pin, xy in {"SCK": esp_pins_R["D18"], "MOSI": esp_pins_R["D23"], "RST": esp_pins_R["D14"], "DIO0": esp_pins_R["D26"]}.items():
    line(d, (1020, 250 if pin != "DIO0" else 300), (xy[0]+30, xy[1]), GREEN, 3)
    d.text(((1020+xy[0])/2+10, (250+xy[1])/2-6 if pin != "DIO0" else (300+xy[1])/2-6), pin, fill=GREEN, font=font(12))
# 3v3 rail
line(d, (340, 920), (600, 920), RED, 4)
line(d, (810, 920), (1150, 920), RED, 4)
line(d, (705, 880), (705, 920), RED, 4)
line(d, (700, 380), (705, 920), RED, 3)
d.text((960, 895), "3.3 V rail", fill=RED, font=font(15))
# divider to D34
line(d, (1080, 770), (860, 650), ORANGE, 3)
d.text((950, 690), "D34", fill=ORANGE, font=font(14))
# battery to tp4056 to buck
arrow(d, (150, 700), (150, 660), RED, 4)
line(d, (330, 780), (600, 800), RED, 4)
d.text((420, 760), "VBAT", fill=RED, font=font(14))
# cap note
d.rounded_rectangle([880, 120, 1330, 260], radius=10, fill=PANEL, outline=BORDER, width=2)
d.text((1105, 140), "470 uF BULK CAP", fill=RED, font=font(17), anchor="mm")
d.text((1105, 175), "directly across Ra-02 3V3/GND pins", fill=TXT, font=font(13), anchor="mm")
d.text((1105, 200), "shortest leads possible", fill=DIM, font=font(13), anchor="mm")
d.text((1105, 225), "absorbs 250-300 mA TX spike", fill=DIM, font=font(13), anchor="mm")
# antenna
line(d, (1140, 120), (1140, 70), GREEN, 5)
d.text((1160, 70), "17.3 cm whip", fill=GREEN, font=font(14))
img.save(f"{OUT}/2_wiring_map.png")

# ============================================================
# 3. POWER TREE
# ============================================================
W, H = 1200, 760
img, d = new(W, H, "POWER TREE - single Li-ion cell")
chain = [
    ("Li-ion 1500 mAh\n3.7 V", ORANGE),
    ("TP4056\nprotection+charge", RED),
    ("SWITCH\n>=1 A", YELLOW),
    ("buck-boost\n-> 3.3 V fixed", PURPLE),
    ("3.3 V RAIL\n+ 470 uF at Ra-02", RED),
]
x = 60
positions = []
for label, c in chain:
    w = 200
    box(d, [x, 160, x+w, 280], label.split("\n")[0], c, label.split("\n")[1], tsize=17)
    positions.append((x, x+w))
    x += w + 90
for i in range(len(positions)-1):
    arrow(d, (positions[i][1], 220), (positions[i+1][0], 220), TXT, 4)
# divider tap from battery side
line(d, (positions[2][1]-40, 280), (positions[2][1]-40, 380), ORANGE, 3)
box(d, [positions[2][1]-140, 380, positions[2][1]+100, 480], "100k/100k divider", ORANGE, "-> D34 sense (21 uA)")
# loads
loads = [
    ("ESP32\n40-50 mA", BLUE),
    ("BMP280\n<1 mA (I2C 21/22)", PURPLE),
    ("SX1278 TX\n120 mA peaks (SPI)", GREEN),
    ("divider\n21 uA (D34)", ORANGE),
]
x = 80
for label, c in loads:
    w = 240
    line(d, (x+w/2, 560), (x+w/2, 500), c, 3)
    box(d, [x, 560, x+w, 670], label.split("\n")[0], c, label.split("\n")[1])
    x += w + 60
line(d, (600, 560), (600+3*300-60, 560), TXT, 3)
d.text((600, 530), "3.3 V rail feeds all four", fill=DIM, font=font(15))
d.text((600, 720), "average ~130-160 mA  ->  9-11 h endurance  |  peaks ~300 mA absorbed by 470 uF", fill=DIM, font=font(15), anchor="mm")
img.save(f"{OUT}/3_power_tree.png")

# ============================================================
# 4. LAUNCH-DAY CHECKLIST
# ============================================================
W, H = 1000, 1200
img, d = new(W, H, "LAUNCH-DAY CHECKLIST")
phases = [
    ("NIGHT BEFORE", GREEN, [
        "Charge battery (TP4056 LED green = done)",
        "Flash final flight_sketch.ino, bench-test TX at 10 Hz",
        "Ground station charged, dashboard loads at 192.168.4.1",
    ]),
    ("PAD SETUP", YELLOW, [
        "Antenna fitted - ALWAYS before battery",
        "Switch ON -> serial shows 'Launch Pressure Set'",
        "Dashboard shows packets at 10 Hz  (= your 'armed' LED)",
        "Battery_V column plausible (3.9-4.2 V)",
        "Static ports clear, sensor dark, bay not sealed",
    ]),
    ("RAIL + FLIGHT", RED, [
        "Insert on rail, confirm telemetry still live",
        "Launch",
        "Watch dashboard: altitude climb -> APOGEE badge -> descent",
    ]),
    ("RECOVERY", BLUE, [
        "Power off via switch before handling",
        "USB -> serial -> send DUMP within 3 s of boot",
        "Save CSV: altitude profile + Battery_V for the post-flight",
    ]),
]
y = 90
for title, c, items in phases:
    d.rounded_rectangle([40, y, 960, y+44], radius=8, fill=PANEL, outline=c, width=2)
    d.text((60, y+22), title, fill=c, font=font(18), anchor="lm")
    y += 58
    for it in items:
        d.ellipse([60, y+8, 76, y+24], outline=c, width=2)
        d.text((92, y+16), it, fill=TXT, font=font(16), anchor="lm")
        y += 34
    y += 18
img.save(f"{OUT}/4_launch_checklist.png")

print("saved:", os.listdir(OUT))
