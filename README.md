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
