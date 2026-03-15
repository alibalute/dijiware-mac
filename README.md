# Etar ESP32 Dev Project

## Overview
This project is an ESP-IDF based firmware for the Etar ESP32 development board. It includes components for USB, MIDI, and other hardware interfaces.

## Project Structure

- **main/**: Application source code.
- **components/**: Custom project components.
- **managed_components/**: Components managed by the IDF component manager (e.g., TinyUSB).
- **firmware/**: Binary firmware artifacts.
- **build/**: Build output directory (generated).

## Getting Started

### Prerequisites
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) installed and configured.
- CMake and Ninja build tools.

### Build
**Ensure the ESP-IDF environment is active** (so `IDF_PATH` points to a valid install). In a new terminal, run the IDF export script first, for example:
```bash
# Example: if ESP-IDF is at ~/.espressif/v5.0.1/esp-idf
source ~/.espressif/v5.0.1/esp-idf/export.sh
# Or: source ~/esp/esp-idf/export.sh  (if you cloned esp-idf there)
```
Then build:
```bash
idf.py build
```

### Flash
To flash the firmware to the device:
```bash
idf.py -p (PORT) flash
```
Replace `(PORT)` with your serial port (e.g., `COM3` on Windows or `/dev/ttyUSB0` on Linux).

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
