import math

W, H = 70, 61

# Pary gradientów CyberNeon (R, G, B) dla 8 modułów
PALETTES = [
    ((0, 245, 255), (230, 0, 255)),   # 0: WIFI (Cyjan -> Fiolet/Magenta)
    ((130, 255, 20), (0, 230, 255)),  # 1: BT (Neon Green -> Cyjan)
    ((255, 0, 140), (255, 190, 0)),   # 2: MESH (Magenta -> Złoto)
    ((0, 240, 255), (255, 30, 160)),  # 3: GEM (Cyjan -> Hot Pink)
    ((255, 215, 0), (255, 0, 130)),   # 4: MPY (Złoto -> Magenta)
    ((170, 50, 255), (0, 255, 210)),  # 5: FLASH (Fiolet -> Turkus)
    ((255, 170, 0), (0, 235, 255)),   # 6: OTA (Pomarańcz -> Cyjan)
    ((255, 0, 160), (0, 245, 255))    # 7: SET (Magenta -> Cyjan)
]

BAYER = ((0, 2), (3, 1))

def sd_seg(px, py, x1, y1, x2, y2):
    vx, vy = x2 - x1, y2 - y1
    wx, wy = px - x1, py - y1
    c1 = vx * wx + vy * wy
    if c1 <= 0:
        return math.hypot(px - x1, py - y1)
    c2 = vx * vx + vy * vy
    if c2 <= c1:
        return math.hypot(px - x2, py - y2)
    b = c1 / c2
    return math.hypot(px - (x1 + b * vx), py - (y1 + b * vy))

def sd_round_box(px, py, cx, cy, bx, by, r):
    qx = abs(px - cx) - (bx - r)
    qy = abs(py - cy) - (by - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r

def sd_icon(idx, x, y):
    cx, cy = 34.5, 21.5
    d = 999.0
    if idx == 0:  # WIFI
        by = 28.0
        d = min(d, max(math.hypot(x - cx, y - by) - 1.5, 0.0))
        dx, dy = x - cx, y - by
        if dy < 0 and abs(dx) < -dy * 1.15:
            r = math.hypot(dx, dy)
            for rad in (5.5, 9.5, 13.5):
                d = min(d, abs(r - rad))
        else:
            for rad in (5.5, 9.5, 13.5):
                ax = rad * 0.75
                ay = -rad * 0.66
                d = min(d, math.hypot(dx - ax, dy - ay), math.hypot(dx + ax, dy - ay))
    elif idx == 1:  # BT
        d = min(d, sd_seg(x, y, cx, 9.5, cx, 33.5))
        d = min(d, sd_seg(x, y, cx, 9.5, cx + 6.5, 15.5))
        d = min(d, sd_seg(x, y, cx + 6.5, 15.5, cx - 6.5, 27.5))
        d = min(d, sd_seg(x, y, cx - 6.5, 15.5, cx + 6.5, 27.5))
        d = min(d, sd_seg(x, y, cx + 6.5, 27.5, cx, 33.5))
    elif idx == 2:  # MESH
        pts = [(cx, 12.0), (cx - 8.5, 27.0), (cx + 8.5, 27.0)]
        for px, py in pts:
            d = min(d, abs(math.hypot(x - px, y - py) - 2.6))
        d = min(d, sd_seg(x, y, pts[0][0] - 1.5, pts[0][1] + 2.2, pts[1][0] + 1.5, pts[1][1] - 2.2))
        d = min(d, sd_seg(x, y, pts[0][0] + 1.5, pts[0][1] + 2.2, pts[2][0] - 1.5, pts[2][1] - 2.2))
        d = min(d, sd_seg(x, y, pts[1][0] + 2.6, pts[1][1], pts[2][0] - 2.6, pts[2][1]))
    elif idx == 3:  # GEM (AI Spark)
        d = min(d, sd_seg(x, y, cx, 9.5, cx, 33.5))
        d = min(d, sd_seg(x, y, cx - 11, cy, cx + 11, cy))
        d = min(d, sd_seg(x, y, cx - 5.5, cy - 5.5, cx + 5.5, cy + 5.5))
        d = min(d, sd_seg(x, y, cx - 5.5, cy + 5.5, cx + 5.5, cy - 5.5))
    elif idx == 4:  # MPY (< / >)
        d = min(d, sd_seg(x, y, cx - 5, 14.5, cx - 10.5, cy))
        d = min(d, sd_seg(x, y, cx - 10.5, cy, cx - 5, 28.5))
        d = min(d, sd_seg(x, y, cx + 5, 14.5, cx + 10.5, cy))
        d = min(d, sd_seg(x, y, cx + 10.5, cy, cx + 5, 28.5))
        d = min(d, sd_seg(x, y, cx + 2, 13.5, cx - 2, 29.5))
    elif idx == 5:  # FLASH (Chip)
        d = min(d, abs(sd_round_box(x, y, cx, cy, 6.5, 6.5, 1.5)))
        for off in (-3.5, 0.0, 3.5):
            d = min(d, sd_seg(x, y, cx - 10, cy + off, cx - 6.5, cy + off))
            d = min(d, sd_seg(x, y, cx + 6.5, cy + off, cx + 10, cy + off))
            d = min(d, sd_seg(x, y, cx + off, cy - 10, cx + off, cy - 6.5))
            d = min(d, sd_seg(x, y, cx + off, cy + 6.5, cx + off, cy + 10))
    elif idx == 6:  # OTA (Download/Cloud)
        d = min(d, sd_seg(x, y, cx, 10.5, cx, 24.0))
        d = min(d, sd_seg(x, y, cx - 5, 19.0, cx, 24.0))
        d = min(d, sd_seg(x, y, cx + 5, 19.0, cx, 24.0))
        d = min(d, sd_seg(x, y, cx - 9, 23.5, cx - 9, 29.0))
        d = min(d, sd_seg(x, y, cx - 9, 29.0, cx + 9, 29.0))
        d = min(d, sd_seg(x, y, cx + 9, 29.0, cx + 9, 23.5))
    elif idx == 7:  # SET (Gear)
        r = math.hypot(x - cx, y - cy)
        d = min(d, abs(r - 7.5), abs(r - 3.0))
        for a_deg in range(0, 360, 45):
            a = math.radians(a_deg)
            d = min(d, sd_seg(x, y, cx + math.cos(a)*7.5, cy + math.sin(a)*7.5, cx + math.cos(a)*10.2, cy + math.sin(a)*10.2))
    return d

def to_rgb565(r, g, b, x, y):
    dith = (BAYER[y & 1][x & 1] - 1.5) * 1.5
    r = int(min(255, max(0, r + dith)))
    g = int(min(255, max(0, g + dith * 0.5)))
    b = int(min(255, max(0, b + dith)))
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

out = ["#pragma once\n#include <Arduino.h>\n"]

for idx in range(8):
    c1, c2 = PALETTES[idx]
    norm_arr = []
    sel_arr = []
    for y in range(H):
        for x in range(W):
            t = max(0.0, min(1.0, (x / float(W)) * 0.65 + (y / float(H)) * 0.35))
            nr = c1[0] + (c2[0] - c1[0]) * t
            ng = c1[1] + (c2[1] - c1[1]) * t
            nb = c1[2] + (c2[2] - c1[2]) * t

            ti = 1.0 - t * 0.7
            ir = c1[0] + (c2[0] - c1[0]) * ti
            ig = c1[1] + (c2[1] - c1[1]) * ti
            ib = c1[2] + (c2[2] - c1[2]) * ti

            sb = sd_round_box(x, y, 34.5, 30.0, 31.0, 26.5, 11.0)
            db = abs(sb)
            di = sd_icon(idx, x, y)

            for is_sel, arr in ((False, norm_arr), (True, sel_arr)):
                if sb < 0:
                    # +35% jaśniejsze tło wewnętrzne dla aktywnego kafelka
                    br, bg, bb = (52, 24, 92) if is_sel else (14, 6, 26)
                else:
                    br, bg, bb = (9, 4, 18)

                # Dużo mocniejsza poświata Gaussa i gorący rdzeń (+30% jasności dla zaznaczonego)
                gb = math.exp(-db * (0.24 if is_sel else 0.58)) * (1.55 if is_sel else 0.70)
                cb = max(0.0, 1.0 - db / (2.4 if is_sel else 1.30))

                gi = math.exp(-di * (0.28 if is_sel else 0.58)) * (1.50 if is_sel else 0.80)
                ci = max(0.0, 1.0 - di / (2.2 if is_sel else 1.40))

                wb = (cb ** 1.3) * (1.15 if is_sel else 0.40)
                wi = (ci ** 1.3) * (1.15 if is_sel else 0.50)

                r = br + nr * (gb + cb * 0.95) + ir * (gi + ci * 0.95) + 255.0 * (wb + wi)
                g = bg + ng * (gb + cb * 0.95) + ig * (gi + ci * 0.95) + 255.0 * (wb + wi)
                b = bb + nb * (gb + cb * 0.95) + ib * (gi + ci * 0.95) + 255.0 * (wb + wi)

                arr.append(f"0x{to_rgb565(r, g, b, x, y):04X}")

    out.append(f"const uint16_t tile_norm_{idx}[4270] PROGMEM = {{ " + ",".join(norm_arr) + " };\n")
    out.append(f"const uint16_t tile_sel_{idx}[4270] PROGMEM = {{ " + ",".join(sel_arr) + " };\n")

out.append("const uint16_t* const TILES_NORM[8] = { tile_norm_0, tile_norm_1, tile_norm_2, tile_norm_3, tile_norm_4, tile_norm_5, tile_norm_6, tile_norm_7 };\n")
out.append("const uint16_t* const TILES_SEL[8] = { tile_sel_0, tile_sel_1, tile_sel_2, tile_sel_3, tile_sel_4, tile_sel_5, tile_sel_6, tile_sel_7 };\n")

with open("src/cyber_assets.h", "w") as f:
    f.writelines(out)

print("[SHADER] Wygenerowano src/cyber_assets.h (+30% Boost na aktywnym kafelku)")
