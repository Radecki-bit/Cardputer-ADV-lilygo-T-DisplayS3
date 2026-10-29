# Smolinski|Labs Dual-MCU Tactical Cluster (`v1.2.0-PRODUCTION`)

**Author:** R. Smoliński (`@whoami`) | CEO Smolinski|Labs  
**Targets:** M5Stack Cardputer ADV (Master Windows XP Luna OS) + LilyGO T-Display-S3 (Tactical HUD)

---

## 1. Sterowanie i Skróty Klawiszowe (M5Stack Cardputer ADV)

| Klawisz / Akcja | Funkcja w systemie Windows XP Luna |
| :--- | :--- |
| **`Fn` + Strzałki (`;` `.` `,` `/`)** | Poruszanie kursorem myszy po ekranie (działa również bezpośrednio bez `Fn` na pulpicie) |
| **`Enter`** | Kliknięcie kursorem w przycisk **`[X]`** (zamyka okno), ikonę **Rekina Bruce**, przycisk **`[start]`** lub opcję w Menu Start |
| **`[M]`** | Otwarcie / Zamknięcie Menu Start (`@whoami \| CEO`) |
| **`[W]`** | Minimalizacja / Przywrócenie okna oscyloskopu `Hydra RF` |
| **`[1]` / `[2]`** | Przełączenie układu radiowego: `1 = CC1101 Sub-GHz` / `2 = nRF24 2.4GHz` |
| **`[3]` / `[F]`** | Przełączenie częstotliwości Sub-GHz: `433.92 MHz <-> 868.35 MHz` |
| **`[4]`** | Przełączenie trybu pracy: `SPECTRUM <-> SNIFFER <-> STANDBY` |
| **`[5]` / `[A]`** | **Fizyczne Włączenie / Wyłączenie gniazda anteny 2.4GHz (`OFF <-> ON`)** |
| **`[6]` / `[B]`** | Przełączenie systemu do **Bruce Firmware** z karty SD (`/firmware/bruce.bin` lub `/bruce.bin`) |
| **`[S]`** | Zapis logu telemetrii RF na kartę SD (`/hydra_rf.log`) |

---

## 2. Sterowanie i Bezpieczny Włącznik Anteny 2.4GHz (LilyGO T-Display-S3)

Gniazdo zewnętrznej anteny 2.4GHz (`GPIO 16`) jest **domyślnie fizycznie wyłączone (`0V / OFF`)** przy każdym starcie systemu, co pozwala na bezpieczne podłączenie anteny na stałe.

| Przycisk na T-Display-S3 | Akcja | Funkcja |
| :--- | :--- | :--- |
| **Górny/Drugi Przycisk (`GPIO 14` - nie-BOOT)** | **Szybki Dwuklik (`2x Klik`)** | **Fizyczne Włączenie / Wyłączenie napięcia gniazda anteny 2.4GHz (`ON <-> OFF`)** |
| **Górny/Drugi Przycisk (`GPIO 14`)** | Pojedynczy Klik | Przełączenie widoku: `Tactical Spectrum HUD <-> 8-Tile Launcher` |
| **Górny/Drugi Przycisk (`GPIO 14`)** | Przytrzymanie (`> 0.7 s`) | Zmiana języka interfejsu (`PL / EN / DE / RU`) |
| **Dolny Przycisk (`GPIO 0` - BOOT)** | Pojedynczy Klik | Przełączenie wykresu: `Spectrum Analyzer <-> Waterfall` |

---

## 3. Pliki Binarne (`releases/` oraz `sd_card_root/`)
* **`SmolinskiLabs_Cardputer_ADV.bin`** — Obraz pod M5Launcher / kartę SD dla M5Stack Cardputer ADV.
* **`SmolinskiLabs_TDisplay_S3.bin`** — Obraz aplikacji (`0x10000`) pod M5Launcher dla LilyGO T-Display-S3.
* **`SmolinskiLabs_TDisplay_S3_FULL_0x0.bin`** — Pełny scalony obraz fabryczny (`bootloader + partitions + app`) do bezpośredniego wgrywania przez Web Flasher (**ESP Terminator**) pod adres **`0x0000`**.


---

# SYSTEM_@whoami — Distributed Operational Memory & Co-Processor Cluster Expansion

**Organization:** Smolinski|Labs  
**Lead Architect:** R. Smoliński | CEO Smolinski|Labs  
**Module Status:** Production Ready (`0x0000` Web Flasher Images Included)  
**RF Band:** 2.44 GHz (Channel 7 — `2442 MHz`) | **Inter-Board Bus:** UART DMA (`RX/TX/5V/GND`)

---

## 1. Executive Overview (Project Continuation)

This module extends the core **M5Stack Cardputer ADV + LilyGO T-Display-S3 (`SYSTEM_@whoami`)** tactical architecture by introducing an **external, wirelessly linked Operational Memory and Distributed Compute Cluster**.

To bypass the internal SRAM/DMA constraints of a standalone microcontroller during intensive tasks (quantized `INT8` micro-LLM / Ollama layer execution, high-throughput telemetry buffering, and spectrum state aggregation), the system offloads memory pages and matrix operations across external **dual-board ESP32 cluster pairs** powered by an independent **5V / 8A star-topology power bus (`JCY-0580`)**.

---

## 2. Hardware Architecture & Topology

```text
SYSTEM_@whoami (Distributed Operational Memory & Compute Cluster)
 ├── 1. Power Distribution Layer (Independent 5V / 8A Rail)
 │    └── 5V 8A PSU ──> Motherboard Host ──> Star Topology 5V/GND Bus ──> Cluster Pairs
 │
 ├── 2. Dual-Board Operational Memory & Compute Nodes (Cluster Pairs)
 │    ├── Node Type A: ESP with External SMA Antenna (ESP_z_antena.bin)
 │    │    ├── Role: RF Master (2.44 GHz CH 7 @ +20.0 dBm) + Memory Pool Shard A
 │    │    └── Links to Partner Node B via Hardwired RX / TX / 5V / GND
 │    └── Node Type B: ESP without External Antenna (ESP_bez_anteny.bin)
 │         ├── Role: Dedicated Dual-Core 240 MHz UART Co-Processor + Memory Pool Shard B
 │         └── Doubles available operational SRAM & ALU throughput per cluster module
 │
 ├── 3. Wireless Aggregation Layer (ESP-NOW 2.44 GHz + BLE 5.0/5.1)
 │    ├── Cluster Pairs transmit merged memory/tensor frames to the Motherboard
 │    └── Motherboard buffers, orders, and bridges packets to M5Stack Cardputer ADV
 │
 └── 4. Headless Hardware LED Telemetry (GPIO 2)
      ├── Boot / Initialization ────────> 5 Hz Fast Blink (100 ms ON / 100 ms OFF)
      ├── Pairing / Searching Master ───> 1 Hz Slow Pulse (500 ms ON / 500 ms OFF)
      ├── Normal Operation (Online) ────> Solid Continuous ON
      ├── Data Transmission (TX) ───────> Single Sharp Blink
      └── Error / Link Lost ────────────> Double Blink with Pause (100/100/100/700 ms)
```

---

## 3. Ready-to-Flash Binary Images (`0x0000` Web Flasher)

| Binary Filename | Target Hardware | Flash Offset | Functional Description |
| :--- | :--- | :--- | :--- |
| **`ESP_z_antena.bin`** | **ESP32-U (With External 2.44 GHz SMA Antenna)** | `0x0000` | Drives the external RF switch (`GPIO 4`) at `+20.0 dBm` (`80 qdBm`) on `2442 MHz` (CH 7), manages `BLE 5.0/5.1` beacons, executes Memory Shard A, and coordinates the partner board over `RX/TX`. |
| **`ESP_bez_anteny.bin`** | **ESP32 DevKit (Without External Antenna)** | `0x0000` | Dedicated `RX/TX` UART Co-Processor (`115200 8N1`). Provides secondary operational SRAM pooling and executes Memory/Tensor Shard B across both `240 MHz` Xtensa LX6 cores. |

---
*Engineered by **R. Smoliński | CEO Smolinski|Labs***
