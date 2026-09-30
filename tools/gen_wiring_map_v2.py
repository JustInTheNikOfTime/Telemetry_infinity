#!/usr/bin/env python3
"""Redraw the wiring map with the Ra-02 fully visible and clean SPI labels."""
from PIL import Image, ImageDraw, ImageFont
import os

OUT = "visualizations"
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

img = Image.new("RGB", (1400, 950), BG)
d = ImageDraw.Draw(img)
d.text((24, 16), "WIRING MAP - flight board (matches firmware pin map)", fill=BLUE, font=font(26))
d.line([(24, 54), (1376, 54)], fill=BORDER, width=2)

def box(xy, label, color, sub=None, fill=PANEL, tsize=17):
    x0, y0, x1, y1 = xy
    d.rounded_rectangle(xy, radius=8, fill=fill, outline=color, width=2)
    cx = (x0+x1)/2
    if sub:
        d.text((cx, (y0+y1)/2-14), label, fill=TXT, font=font(tsize), anchor="mm")
        d.text((cx, (y0+y1)/2+14), sub, fill=DIM, font=font(12), anchor="mm")
    else:
        d.text((cx, (y0+y1)/2), label, fill=TXT, font=font(tsize), anchor="mm")

def line(p0, p1, color, w=3):
    d.line([p0, p1], fill=color, width=w)

# ---- ESP32 center ----
ESP = [560, 400, 840, 700]
box(ESP, "ESP32 DevKit", BLUE, "80 MHz", fill=(25, 40, 65))
# pin stubs, left side (inputs) and right side (SPI out)
pinsL = {"D21": 460, "D22": 500, "D19": 540, "D5": 580, "D34": 640}
pinsR = {"D18": 460, "D23": 500, "D14": 540, "D26": 580}
for p, y in pinsL.items():
    d.text((ESP[0]-8, y), p, fill=BLUE, font=font(14), anchor="rm")
    line((ESP[0]-8, y), (ESP[0]-40, y), BLUE, 2)
for p, y in pinsR.items():
    d.text((ESP[2]+8, y), p, fill=BLUE, font=font(14), anchor="lm")
    line((ESP[2]+8, y), (ESP[2]+40, y), BLUE, 2)

# ---- Ra-02 top-right (fully visible now) ----
RA = [1010, 120, 1270, 330]
box(RA, "Ra-02 SX1278", GREEN, "ANT ON BEFORE POWER")
# pin stubs on its left edge
ra_pins = {"NSS": 160, "SCK": 200, "MOSI": 240, "MISO": 280, "RST": 310, "DIO0": 340}
for p, y in ra_pins.items():
    d.text((RA[0]+10, y), p, fill=GREEN, font=font(12), anchor="lm")
    line((RA[0], y), (RA[0]-25, y), GREEN, 2)
# antenna whip
line((1140, 120), (1140, 70), GREEN, 5)
d.text((1160, 70), "17.3 cm whip", fill=GREEN, font=font(14))

# ---- BMP280 top-left ----
BP = [150, 140, 390, 330]
box(BP, "BMP280", PURPLE, "addr 0x76 / 0x77")
d.text((BP[0]+10, 250), "SDA", fill=PURPLE, font=font(12), anchor="lm")
d.text((BP[0]+10, 290), "SCL", fill=PURPLE, font=font(12), anchor="lm")
line((BP[0], 250), (BP[0]-25, 250), PURPLE, 2)
line((BP[0], 290), (BP[0]-25, 290), PURPLE, 2)

# ---- wiring: BMP -> D21/D22 (route around left) ----
line((BP[0]-25, 250), (90, 250), PURPLE, 3)
line((90, 250), (90, 460), PURPLE, 3)
line((90, 460), (ESP[0]-40, 460), PURPLE, 3)
line((BP[0]-25, 290), (60, 290), PURPLE, 3)
line((60, 290), (60, 500), PURPLE, 3)
line((60, 500), (ESP[0]-40, 500), PURPLE, 3)

# ---- wiring: Ra-02 SPI ----
def route(y0, y1, xmid=940, color=GREEN):
    line((RA[0]-25, y0), (xmid, y0), color, 3)
    line((xmid, y0), (xmid, y1), color, 3)
    line((xmid, y1), (ESP[2]+40, y1), color, 3)
route(ra_pins["NSS"],  pinsL["D5"])   # NSS -> D5 (crosses to left stub via top? keep simple: direct)
# fix: NSS should go to LEFT side D5. Draw explicit left route:
d.rectangle([0,0,1,1], fill=BG)  # no-op
# Undo the simple route by overwriting with BG where it crossed the ESP32:
# (cleaner: redraw the ESP32 box on top)
# redraw ESP32 to cover any crossing lines
d.rounded_rectangle(ESP, radius=8, fill=(25, 40, 65), outline=BLUE, width=2)
d.text(((ESP[0]+ESP[2])/2, 520), "ESP32 DevKit", fill=TXT, font=font(17), anchor="mm")
d.text(((ESP[0]+ESP[2])/2, 545), "80 MHz", fill=DIM, font=font(12), anchor="mm")
# re-stub pins after redraw
for p, y in pinsL.items():
    d.text((ESP[0]-8, y), p, fill=BLUE, font=font(14), anchor="rm")
    line((ESP[0]-8, y), (ESP[0]-40, y), BLUE, 2)
for p, y in pinsR.items():
    d.text((ESP[2]+8, y), p, fill=BLUE, font=font(14), anchor="lm")
    line((ESP[2]+8, y), (ESP[2]+40, y), BLUE, 2)
# NSS route on the left side, over the top of the ESP32 (clear corridor y=110..130)
line((RA[0]-25, ra_pins["NSS"]), (900, 110), GREEN, 3)
line((900, 110), (900, 90), GREEN, 3)
line((900, 90), (430, 90), GREEN, 3)
line((430, 90), (430, 580), GREEN, 3)
line((430, 580), (ESP[0]-40, 580), GREEN, 3)
d.text((640, 78), "NSS -> D5", fill=GREEN, font=font(13))
# MISO (ra 280) -> D19 (left 540) via left corridor x=470
line((RA[0]-25, 280), (940, 280), GREEN, 3)
line((940, 280), (940, 350), GREEN, 3)
line((940, 350), (470, 350), GREEN, 3)
line((470, 350), (470, 540), GREEN, 3)
line((470, 540), (ESP[0]-40, 540), GREEN, 3)
d.text((640, 338), "MISO -> D19", fill=GREEN, font=font(13))
# SCK/D23, MOSI/D23? correct mapping: SCK->D18, MOSI->D23, RST->D14, DIO0->D26 (right side)
route(ra_pins["SCK"],  pinsR["D18"], xmid=960)
d.text((800, 188), "SCK -> D18", fill=GREEN, font=font(13))
route(ra_pins["MOSI"], pinsR["D23"], xmid=980)
d.text((800, 228), "MOSI -> D23", fill=GREEN, font=font(13))
route(ra_pins["RST"],  pinsR["D14"], xmid=1000)
route(ra_pins["DIO0"], pinsR["D26"], xmid=1020)

# ---- power row ----
TP = [100, 720, 330, 870]
box(TP, "TP4056 -> switch", RED, "battery path")
BB = [600, 760, 810, 890]
box(BB, "buck-boost", YELLOW, "3.3 V fixed")
DV = [1090, 720, 1340, 870]
box(DV, "divider 100k/100k", ORANGE, "junction -> D34")
# VBAT wire
line((TP[2], 780), (BB[0], 800), RED, 4)
d.text((450, 762), "VBAT after switch", fill=RED, font=font(14))
# 3V3 rail
line((BB[2], 830), (1340, 830), RED, 4)
line((700, 700), (700, 830), RED, 4)
d.text((980, 850), "3.3 V rail -> ESP32, BMP280, Ra-02 (+470 uF)", fill=RED, font=font(15))
# divider tap from battery side
line((DV[0], 760), (DV[0]-60, 640), ORANGE, 3)
line((DV[0]-60, 640), (ESP[2]+40, 640), ORANGE, 3)
d.text((980, 620), "D34", fill=ORANGE, font=font(14))

# ---- cap note (moved to free space, no overlap) ----
CN = [100, 380, 420, 620]
d.rounded_rectangle(CN, radius=10, fill=PANEL, outline=BORDER, width=2)
d.text((260, 405), "470 uF BULK CAP", fill=RED, font=font(17), anchor="mm")
for i, t in enumerate(["directly across Ra-02 3V3/GND pins",
                       "shortest possible leads",
                       "absorbs 250-300 mA TX spike",
                       "stripe (negative) to GND"]):
    d.text((120, 445+i*30), "- " + t, fill=TXT if i < 2 else DIM, font=font(14))

img.save(f"{OUT}/2_wiring_map.png")
print("redrawn")
