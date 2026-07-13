# Patent figures (dijiware-mac / Dijilele firmware disclosure)

Black-and-white SVG diagrams for `../PATENT_DISCLOSURE_Dijiware.md`.

| File | Figure |
|------|--------|
| `FIG_1_hardware_system.svg` | MCU, sensors, codec, BLE/USB/Wi-Fi |
| `FIG_2_freertos_tasks.svg` | Task priorities, ADC mutex, deferred calibration |
| `FIG_3_strum_detection.svg` | STRUM_DERIVATIVE algorithm in etar.c |
| `FIG_4_ble_control_sysex.svg` | 0xFE control plane, telemetry, SysEx MIDI upload |
| `FIG_5_strum_step_integration.svg` | eTar, pic-midi, midi_player strum-step path |
| `FIG_6_wifi_ota_pipeline.svg` | Soft-AP, HTTP server, flash OTA |
| `FIG_7_note_generation.svg` | Sensors to getNote to MIDI fan-out |

Companion-app figures live in `diji_app_flutter/docs/patent-figures/`.

## Viewing

```bash
open docs/patent-figures/FIG_1_hardware_system.svg
```

All files use ASCII-only text for valid XML. Validate with:

```bash
xmllint --noout docs/patent-figures/*.svg
```

## Export for filing

Convert to PDF/TIFF at 300 dpi for USPTO/EPO formal drawings (Inkscape, Illustrator, or patent draftsman).
