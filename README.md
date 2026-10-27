# Smolinski | Labs Tactical Cluster OS

**Author:** @whoami (Radek | Smolinski | Labs)  
**Architecture:** ESP32-S3 (N16R8 - 16MB Flash / 8MB PSRAM)  
**Target Hardware:** LilyGO T-Display-S3 / M5Stack Cardputer ADV Cluster

---

## 🚀 Overview & Design Philosophy

**Tactical Cluster OS** is a high-performance, custom-tailored embedded operating system designed specifically for portable ESP32-S3 security and mesh nodes. Built around a striking **CyberNeon aesthetic** and a practical field-operable architecture, the system is optimized for responsive local control, realtime telemetry, and resilient off-grid wireless operations.

The firmware is structured as a dual-core FreeRTOS environment, with display rendering and fast UI response separated from system intelligence, wireless scanning, and telemetry processing.

---

## 📹 Video Showcases & Field Tests (TikTok)

Follow live hardware builds, RF/LoRa mesh tests, and firmware updates on TikTok:  
👉 **[Official TikTok Profile — Smolinski | Labs (@whoami)](https://www.tiktok.com)**

---

## ⚡ Key Features

* **Dual-Core FreeRTOS Engine:** Dedicated GUI DMA rendering on Core 1 and cluster compute / telemetry / Wi-Fi scanning on Core 0.
* **SDF CyberNeon Rasterizer:** Pre-rendered RGB565 tile assets with Gaussian-style neon glows, dynamic hot-spot borders, and real-time dithered gradients.
* **Immutable Boot Signature:** Custom grunge **LAUNCHER** boot sequence with digital glitch effects and integrity checks on boot.
* **Dynamic Grid & Module Management:** Active 8-tile desktop supporting live module uninstallation with automatic shifting and second-page expansion preparation.
* **Tactical Stealth Mode:** Multi-stage display backlight and hardware LED dimming (`100%` → `45%` → `10% Night` → `0% Stealth Blackout`).
* **Hardware Antenna Switching:** Control over the external 2.4GHz u.FL/IPEX RF front-end.
* **Software Soft Reboot:** Instant in-system reboot triggered directly from the settings menu to loop the boot splash without touching physical bootloaders.

---

## 🛠️ System Modules

1. **WIFI SCANNER:** Real-time 2.4GHz spectrum auditing and SSID enumeration.
2. **BLUETOOTH:** BLE 5.0 controller and advertising packet scanner.
3. **MESH LORA:** 868 MHz SPI transceiver link handler for off-grid mesh nodes.
4. **GEMINI AI:** Air-gapped serial bridge for local LLM agent communication.
5. **MICROPYTHON:** Integrated v1.22 runtime and REPL terminal interface.
6. **FLASH SYS:** Live storage telemetry, 16MB QIO mapping, and free PSRAM gauges.
7. **OTA UPDATE:** Secure local firmware update channel.
8. **SETTINGS:** System configuration, brightness controls, manual clock sync, and soft reboot.

---

## ⚙️ Build & Flash (PlatformIO)

To compile and upload the firmware to your LilyGO T-Display-S3:

```bash
pio run --target upload
```

This project is configured for PlatformIO and expects the ESP32-S3 toolchain and target board definitions to be available in the local environment.

---

## 🧩 Repository Highlights

* **Firmware-first design:** purpose-built for embedded systems and low-level hardware control.
* **Graphics-rich UI:** custom neon raster assets and optimized display rendering.
* **Field-oriented features:** mesh networking, scanning, OTA updates, and diagnostics.
* **Security-oriented boot flow:** immutable startup checks and custom partitioning for a hardened deployment model.

---

*(c) 2026 Smolinski | Labs. All rights reserved.*
