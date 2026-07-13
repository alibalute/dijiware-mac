# Etar ESP32 Dev Project

## Overview
This project is an ESP-IDF based firmware for the Dijilele ESP32 development board. It includes components for USB, MIDI, and other hardware interfaces.

## Project Structure

- **main/**: Application source code.
- **components/**: Custom project components.
- **managed_components/**: Components managed by the IDF component manager (e.g., TinyUSB).
- **firmware/**: Binary firmware artifacts.
- **build/**: Build output directory (generated).

## Hardware profiles (4 MB vs 16 MB flash)

Use the build scripts — each sets flash partitions and a **product ID** sent to DijiApp over BLE (`0x5D`):

| Script | Flash | Default product ID | Product |
|--------|-------|--------------------|---------|
| `./build-4mb.sh` | 4 MB | 1 | Dijilele M |
| `./build-16mb.sh` | 16 MB | 2 | Dijilele S |

Override product ID explicitly when needed:

```bash
DIJILELE_PRODUCT_ID=1 ./build-16mb.sh build   # Dijilele M on 16 MB hardware
DIJILELE_PRODUCT_ID=2 ./build-4mb.sh build  # Dijilele S on 4 MB hardware
```

`DIJILELE_PRODUCT_ID` selects both the BLE product ID and the instrument profile (`INST_DIJILELE_M` / `INST_DIJILELE_S`); you no longer need to edit `main/etar.h` for normal builds.

### Firmware `.bin` output

After `build`, CMake copies the flash image to `firmware/` using the **product slug + version** (same naming DijiApp uses for OTA downloads):

| Product ID | Example output (`version.txt` = `1.0.4`) |
|------------|---------------------------------------------|
| 1 | `firmware/dijilele-m-1-0-4.bin` |
| 2 | `firmware/dijilele-s-1-0-4.bin` |

Host that file on dijilele.com with a matching manifest, e.g. `dijilele-firmware-latest-dijilele-s.json`.

The raw ESP-IDF artifact is still `build/dijilele.bin`. Product ID is compiled **into** the binary (BLE telemetry `0x5D`); the filename reflects which build profile you used so uploads match the right instrument line.

After each successful build, `update-ota-manifest.sh` refreshes `firmware/ota/dijilele-firmware-latest-<slug>.json` and the matching `.txt` from `version.txt` for the product you built. Upload those with the `.bin` to dijilele.com.

## Getting Started

### Prerequisites
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) installed and configured.
- CMake and Ninja build tools.

### Build

**Ensure the ESP-IDF environment is active** (so `IDF_PATH` points to a valid install). In a new terminal, run the IDF export script first, for example:
```bash
source ~/esp/esp-idf/export.sh
```

Pick the script that matches your board’s SPI flash size:

| Hardware | Flash | Script |
|----------|-------|--------|
| Older / compact boards | 4 MB | `./build-4mb.sh` |
| Current 16 MB boards | 16 MB | `./build-16mb.sh` |

```bash
./build-16mb.sh build
# or
./build-4mb.sh build
```

Partition tables: `partitions_4mb.csv` and `partitions_16mb.csv`.  
If you switch between 4 MB and 16 MB on the same checkout, run `idf.py fullclean` once before the first build with the new profile.

Plain `idf.py build` still works but uses whatever is already in `sdkconfig` (may be stale after a profile change).

### Flash
To flash the firmware to the device (16 MB example):
```bash
./build-16mb.sh -p PORT flash
```
Replace `PORT` with your serial port (e.g. `COM3` on Windows or `/dev/tty.usbserial-*` on macOS).

4 MB boards:
```bash
./build-4mb.sh -p PORT flash
```

### Monitor
To view serial output:
```bash
idf.py -p (PORT) monitor
```

## Quality Control
This project uses the following tools for quality assurance:
- **Code Style**: `.clang-format` is provided. Run `idf.py clang-check` or configure your IDE to use it.
- **Testing**: Unity-based tests are located in `test/`. Run tests via `idf.py test` (requires configuration).

## License
[License Information Here]
