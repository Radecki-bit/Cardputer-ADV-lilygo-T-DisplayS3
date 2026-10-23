# Smolinski|Labs Tactical Cluster OS
**Author:** @whoami (R. Smolinski | CEO Smolinski|Labs)  
**Architecture:** ESP32-S3 (N16R8 - 16MB Flash / 8MB PSRAM)  
**Target Hardware:** LilyGO T-Display-S3 / M5Stack Cardputer ADV Cluster  

## Video Showcases & Field Tests (TikTok)
Follow live hardware builds, RF/LoRa mesh tests, and firmware updates on TikTok:  
**[Official TikTok Profile — Smolinski|Labs (@whoami)](https://www.tiktok.com/)**

## Key Features
- **Dual-Core FreeRTOS Engine:** Dedicated GUI DMA rendering on Core 1 & Cluster Compute/Telemetry on Core 0.
- **Immutable Boot Signature:** Cryptographic CRC32 checksum lock verifying @whoami & Smolinski|Labs identity on boot.
- **Custom 16MB Partition Table:** Multi-slot OTA (smolinski_os, slot_alpha, slot_beta) + 4.8MB SPIFFS storage.
- **Hardware Antenna Switching:** Control over external 2.4GHz u.FL/IPEX RF front-end.

## Build & Flash (PlatformIO)
```bash
pio run --target upload
