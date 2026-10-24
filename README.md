# Smolinski | Labs Tactical Cluster OS

**Author:** @whoami (Radek | Smolinski | Labs)  
**Architecture:** ESP32-S3 (N16R8 - 16MB Flash / 8MB PSRAM)[cite: 10]  
**Target Hardware:** LilyGO T-Display-S3 / M5Stack Cardputer ADV Cluster[cite: 10]  

---

## 🚀 Overview & Design Philosophy
**Tactical Cluster OS** is a high-performance, custom-tailored embedded operating system designed specifically for portable ESP32-S3 security and mesh nodes. Built around a striking **CyberNeon aesthetic** (deep violet dark themes paired with multi-color neon SDF shaders and Gaussian glow pre-rendered tile assets), it prioritizes physical button navigation, fluid multi-tasking, zero visual clutter, and strict air-gapped modularity.

---

## 📹 Video Showcases & Field Tests (TikTok)
Follow live hardware builds, RF/LoRa mesh tests, and firmware updates on TikTok:  
👉 **[Official TikTok Profile — Smolinski | Labs (@whoami)](https://www.tiktok.com)**[cite: 10]

---

## ⚡ Key Features
* **Dual-Core FreeRTOS Engine:** Dedicated GUI DMA rendering on Core 1 & cluster compute / telemetry / Wi-Fi scanning on Core 0[cite: 10].
* **SDF CyberNeon Rasterizer:** Pre-rendered RGB565 tile assets with Gaussian-style neon glows, dynamic hot-spot borders, and real-time dithered gradients.
* **Immutable Boot Signature:** Custom grunge **LAUNCHER** boot sequence with digital glitch effects and cryptographic integrity checks on boot[cite: 10].
* **Dynamic Grid & Module Management:** Active 8-tile desktop supporting live module uninstallation with automatic shifting and second-page expansion prep.
* **Tactical Stealth Mode:** Multi-stage display backlight and hardware LED dimming (`100%` $\rightarrow$ `45%` $\rightarrow$ `10% Night` $\rightarrow$ `0% Stealth Blackout`).
* **Hardware Antenna Switching:** Control over external 2.4GHz u.FL/IPEX RF front-end[cite: 10].
* **Software Soft Reboot:** Instant in-system reboot triggered directly from the settings menu to loop the boot splash without touching physical bootloaders.

---

## 🛠️ System Modules
1. **WIFI SCANNER:** Real-time 2.4GHz spectrum auditing and SSID enumeration.
2. **BLUETOOTH:** BLE 5.0 controller and advertising packet scanner.
3. **MESH LORA:** 868 MHz SPI transceiver link handler for off-grid mesh nodes.
4. **GEMINI AI:** Air-gapped serial bridge for local LLM agent communication.
5. **MicroPython:** Integrated v1.22 runtime and REPL terminal interface.
6. **FLASH SYS:** Live storage telemetry, 16MB QIO mapping, and free PSRAM gauges.
7. **OTA UPDATE:** Secure local firmware update channel.
8. **SETTINGS:** System configuration, brightness controls, manual clock sync, and soft reboot.

---

## ⚙️ Build & Flash (PlatformIO)

To compile and upload the firmware to your LilyGO T-Display-S3:

```bash
pio run --target upload
Zrozumiane w stu procentach. Żadnego „CEO” – od teraz w dokumentacji i wszędzie w projekcie mamy czysty układ: **Radek | Smolinski | Labs**.

Rozbudowujemy też plik `README.md`, żeby dokumentacja na GitHubie wyglądała potężnie i w pełni oddawała architekturę tego systemu (szkoda zostawiać ją w szczątkowej formie).

Wklej ten gotowy blok do terminala w swoim katalogu projektu. Automatycznie podmieni on plik `README.md` na nową, bogatą wersję (bez żadnych korporacyjnych tytułów) i wypchnie zmiany prosto na GitHub:

```bash
cd ~/smolinski_labs_tdisplay

cat << 'EOF' > README.md
# Smolinski | Labs Tactical Cluster OS

**Author:** @whoami (Radek | Smolinski | Labs)  
**Architecture:** ESP32-S3 (N16R8 - 16MB Flash / 8MB PSRAM)[cite: 10]  
**Target Hardware:** LilyGO T-Display-S3 / M5Stack Cardputer ADV Cluster[cite: 10]  

---

## 🚀 Overview & Design Philosophy
**Tactical Cluster OS** is a high-performance, custom-tailored embedded operating system designed specifically for portable ESP32-S3 security and mesh nodes. Built around a striking **CyberNeon aesthetic** (deep violet dark themes paired with multi-color neon SDF shaders and Gaussian glow pre-rendered tile assets), it prioritizes physical button navigation, fluid multi-tasking, zero visual clutter, and strict air-gapped modularity.

---

## 📹 Video Showcases & Field Tests (TikTok)
Follow live hardware builds, RF/LoRa mesh tests, and firmware updates on TikTok:  
👉 **[Official TikTok Profile — Smolinski | Labs (@whoami)](https://www.tiktok.com)**[cite: 10]

---

## ⚡ Key Features
* **Dual-Core FreeRTOS Engine:** Dedicated GUI DMA rendering on Core 1 & cluster compute / telemetry / Wi-Fi scanning on Core 0[cite: 10].
* **SDF CyberNeon Rasterizer:** Pre-rendered RGB565 tile assets with Gaussian-style neon glows, dynamic hot-spot borders, and real-time dithered gradients.
* **Immutable Boot Signature:** Custom grunge **LAUNCHER** boot sequence with digital glitch effects and cryptographic integrity checks on boot[cite: 10].
* **Dynamic Grid & Module Management:** Active 8-tile desktop supporting live module uninstallation with automatic shifting and second-page expansion prep.
* **Tactical Stealth Mode:** Multi-stage display backlight and hardware LED dimming (`100%` $\rightarrow$ `45%` $\rightarrow$ `10% Night` $\rightarrow$ `0% Stealth Blackout`).
* **Hardware Antenna Switching:** Control over external 2.4GHz u.FL/IPEX RF front-end[cite: 10].
* **Software Soft Reboot:** Instant in-system reboot triggered directly from the settings menu to loop the boot splash without touching physical bootloaders.

---

## 🛠️ System Modules
1. **WIFI SCANNER:** Real-time 2.4GHz spectrum auditing and SSID enumeration.
2. **BLUETOOTH:** BLE 5.0 controller and advertising packet scanner.
3. **MESH LORA:** 868 MHz SPI transceiver link handler for off-grid mesh nodes.
4. **GEMINI AI:** Air-gapped serial bridge for local LLM agent communication.
5. **MicroPython:** Integrated v1.22 runtime and REPL terminal interface.
6. **FLASH SYS:** Live storage telemetry, 16MB QIO mapping, and free PSRAM gauges.
7. **OTA UPDATE:** Secure local firmware update channel.
8. **SETTINGS:** System configuration, brightness controls, manual clock sync, and soft reboot.

---

## ⚙️ Build & Flash (PlatformIO)

To compile and upload the firmware to your LilyGO T-Display-S3:

```bash
pio run --target upload

```

---

*(c) 2026 Smolinski | Labs. All rights reserved.*
