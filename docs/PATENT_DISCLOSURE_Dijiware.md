# INVENTION DISCLOSURE / PROVISIONAL PATENT APPLICATION DRAFT

**Title:** Embedded Firmware and Method for Sensing, Expressive MIDI Generation, and Wireless Configuration of an Electronic String Instrument

**Applicant / Assignee:** [Dijilele / Your legal entity name]  
**Inventor(s):** [Full legal name(s)]  
**Address:** [Address]  
**Date of invention / reduction to practice:** [Date]  
**Document version:** 1.0 — [Date]  
**Codebase:** `dijiware-mac` (ESP-IDF firmware for Dijilele / eTar)

---

> **Important notice:** This document is a technical invention disclosure drafted from the Dijilele embedded firmware (`dijiware-mac`). It complements the companion-app disclosure in `diji_app_flutter/docs/PATENT_DISCLOSURE_DijiApp.md`. It is **not legal advice** and is **not** a filed patent. Engage a patent attorney for prior-art search, claim drafting, and filing.

---

## ABSTRACT

An electronic string instrument embeds firmware on an ESP32-class microcontroller that converts physical strumming and fretting into MIDI output over BLE-MIDI, USB-MIDI, and an internal synthesizer path. Per-string strum sensors are sampled in a high-priority FreeRTOS task. Strum onsets are detected using a time-derivative of deflection samples (one-sided differentiator over a sliding pre-sample window), with a dynamically drifting rest center (`potMidValue`) that auto-compensates mechanical mis-centering without user calibration.

The firmware exposes a unified control plane by repurposing an unused MIDI status byte (`0xFE`) to dispatch dozens of configuration opcodes (`handleMessage`), including instrument preset, microtonal pitch system, chord mode, hammer-on, sympathetic resonance, metronome, recording, and strum-step pedagogy. Large MIDI files are uploaded from a companion device via a proprietary SysEx framing protocol with base64 payload chunks, stored on SPIFFS, and selected by slot index.

A strum-step mode parses a MIDI file into discrete note-on events; each physical strum (via `midi_strum_step_try_note`) advances one event while suppressing timed sequencer playback—allowing the musician to control tempo by strumming. Firmware version is telemetered over BLE using pseudo-status codes `0x57`/`0x58`/`0x59`. Field updates are performed by activating an on-device Wi-Fi access point and HTTP server (`POST /api/update`), triggerable by a physical button long-press or BLE opcode `0x5C`.

---

## FIELD OF THE INVENTION

Embedded systems, electronic musical instruments, real-time sensor fusion, MIDI protocol extensions, BLE-MIDI GATT services, SPIFFS media storage, over-the-air firmware update, and microtonal pitch mapping for fretless or membrane fingerboard instruments (setar, tar, ukulele-class form factors).

---

## BACKGROUND OF THE INVENTION

Guitar-style MIDI controllers typically use discrete fret switches or capacitive matrices. They rarely combine:

- Continuous strum-bar deflection with derivative-based onset detection and release-triggered note articulation.
- Auto-drifting rest-center calibration during idle periods.
- Per-string enable, hammer-on windows, chord voicing by strum direction, and quarter-tone pitch bends in one firmware image.
- Bidirectional BLE-MIDI that carries both performance data and a parallel binary control channel on the same GATT characteristic.
- Chunked SysEx upload of Standard MIDI Files into instrument flash without USB mass storage.
- Self-paced “strum-step” practice where the player's strum rate replaces a sequencer clock.

The Dijilele / eTar firmware addresses these gaps in a single ESP-IDF project targeting ESP32-S3 (or compatible) with external ADC (TLA2518), accelerometer (MC3419), MP3/MIDI codec (VS1103B), and Bluedroid BLE stack.

---

## BRIEF DESCRIPTION OF THE DRAWINGS

Diagram source files (black-and-white SVG, reference numerals) are in [`docs/patent-figures/`](patent-figures/):

| Figure | File | Description |
|--------|------|-------------|
| **FIG. 1** | [`FIG_1_hardware_system.svg`](patent-figures/FIG_1_hardware_system.svg) | Hardware block diagram: MCU, sensors, synthesizer, BLE/USB/Wi-Fi |
| **FIG. 2** | [`FIG_2_freertos_tasks.svg`](patent-figures/FIG_2_freertos_tasks.svg) | FreeRTOS task architecture and ADC mutex discipline |
| **FIG. 3** | [`FIG_3_strum_detection.svg`](patent-figures/FIG_3_strum_detection.svg) | Strum derivative detection and dynamic rest center |
| **FIG. 4** | [`FIG_4_ble_control_sysex.svg`](patent-figures/FIG_4_ble_control_sysex.svg) | BLE-MIDI 0xFE control plane and SysEx MIDI upload |
| **FIG. 5** | [`FIG_5_strum_step_integration.svg`](patent-figures/FIG_5_strum_step_integration.svg) | Strum-step mode integration across eTar and MIDI player |
| **FIG. 6** | [`FIG_6_wifi_ota_pipeline.svg`](patent-figures/FIG_6_wifi_ota_pipeline.svg) | On-device Wi-Fi AP, HTTP server, and flash OTA pipeline |
| **FIG. 7** | [`FIG_7_note_generation.svg`](patent-figures/FIG_7_note_generation.svg) | Fret + strum to MIDI note generation and output paths |

---

## DETAILED DESCRIPTION OF PREFERRED EMBODIMENTS

### 1. Hardware Platform (FIG. 1)

**Microcontroller 100** — ESP32-S3 running ESP-IDF with FreeRTOS.

**Analog front end 102** — TLA2518 (or equivalent) SPI ADC reads per-string strum potentiometers and fret/membrane position via `readAndAverageStrum()` / `readAndAverageString()` guarded by `xADCSemaphore`.

**Motion sensor 104** — MC3419 accelerometer supports orientation-aware features (e.g., vibrato disabled when instrument laid flat, `joyValue < 400`).

**Audio/MIDI codec 106** — VS1103B receives UART MIDI from `uartmidi_send_message()` for onboard synthesis.

**Connectivity 108** — Bluedroid BLE GATT (blemidi component), USB TinyUSB MIDI, Wi-Fi soft-AP (`Wifi.cpp`).

**Storage 110** — SPIFFS partition holds settings JSON, MIDI slots `/spiffs/1.mid` … `/spiffs/10.mid`, and recording buffers.

**Human interface 112** — GPIO expander (MCP23S08), LEDs, power button (long-press triggers Wi-Fi OTA same as BLE `0x5C`).

Compile-time instrument profiles in `etar.h`: `INST_DIJILELE_S`, `INST_DIJILELE_M`, `INST_TANBOUR`; membrane variants `HALF_CIRCUIT_MEMBRANE` / `FULL_CIRCUIT_MEMBRANE`; strum modes `STRUM_DERIVATIVE` vs `STRUM_DEFLECTION`.

### 2. Real-Time Task Architecture (FIG. 2)

| Task | Priority (embodiment) | Role |
|------|----------------------|------|
| `eTarTask` | 12 (`ETAR_TASK_PRIORITY`) | Strum/fret sampling, note generation hot path |
| `task_ble_midi` | moderate | BLE timestamp tick, version/settings push on connect |
| `xWifiTask` | 6 | Soft-AP + `WebServer` when `wifiOn` |
| `xButtonsTask` | 6 | Power, calibration triggers |
| Bluedroid | ~19 | BLE stack (eTar stays below to avoid starvation) |

**ADC mutex discipline 200** — Strum calibration (`strumCalibrate`) touches ADC/SPI and must not run inside BLE or USB MIDI callbacks. `util_schedule_strum_calibrate()` sets `g_strum_calib_pending`; `util_run_pending_strum_calibrate_from_etar()` executes only inside `eTarTask`, preventing races on `xADCSemaphore`.

**Boot sequencing 202** — `main.c` waits for `boot_intro_done` before loading settings from flash so boot MIDI does not interleave with user settings and cause spurious notes.

### 3. Strum Detection — STRUM_DERIVATIVE Mode (FIG. 3)

For each string `atString` in `eTarTask` main loop (`etar.c`):

1. **Sample** — Average two consecutive `readAndAverageStrum(atString)` readings.
2. **Direction** — Compare sample to `potMidValue[atString]` to classify UP / DOWN / REST (used for chord mode major/minor selection).
3. **Deflection** — `currentSampleN = sensitivity * |sample - potMidValue|`.
4. **Derivative** — One-sided four-sample approximation (Holoborodko):
   ```
   strumDerivative = sensitivity * ((currentSampleN - preSampleN2) + (preSampleN1 - preSampleN3))
   ```
5. **Onset** — If `!inStrum` and `strumDerivative > derivativeThreshold`, mark strum detected; read fret ADC average (10 samples with yields).
6. **Release** — If `inStrum` and `strumDerivative < derivativeReturnThreshold`, release strum; optionally `noteOn` on release when `noteOnReleaseEnabled`.
7. **Dynamic center 300** — When `!inStrum` and deflection below `STRUM_CENTER_REST_THRESH`:
   ```
   potMidValue += STRUM_CENTER_ALPHA * (sample - potMidValue)
   ```
8. **Auto calibration 302** — `AUTO_STRUM_CALIBRATION`: on release, set `autoStrumCalibration`; `strumCalibrate()` writes rest averages into `potMidValue[]`. Triggered from BLE opcode `0x02` (deferred to eTar task).

**Velocity 304** — Peak deflection across pre-sample window maps to MIDI velocity; user-configurable `strumVelOutMin` / `strumVelOutMax` (opcodes `0x5A`/`0x5B`) when dynamics enabled.

Alternative compile-time path `STRUM_DEFLECTION` uses absolute threshold with hysteresis instead of derivative.

### 4. Fret Position and Microtonal Pitch (FIG. 7)

`pic-midi.c` maps membrane ADC ratios through instrument-specific `fretRatio[]` tables (ukulele 13-position half-membrane, setar 41-position, etc.).

**Pitch systems** (`util.c`): EDO-24, Jazayeri, EDO-51 Turkish, EDO-51 Sharif, custom (opcode `0x34`). Quarter-tone mode (`quarterNotesEnabled`, opcode `0x15`) routes pitch bends on dedicated channel with per-target center offsets (`ETAR_QUARTERTONE_OFFSET`, etc.).

**Per-string tuning** — Opcodes `0x49`–`0x4C` enable/disable strings; pitch indices sent from companion app map to internal note grid (middle C index 120, not GM 60).

### 5. Expressive Performance Features

| Feature | Mechanism |
|---------|-----------|
| Chords mode | `chordEnabled`; strum direction sets major vs minor triad (`chordType`) |
| Hammer-on | `hammerOnEnabled`; fret change within `HAMMER_ON_WINDOW_TICKS` after note; lockout `hammerOnLockoutUntilTick`; velocity % of last strum (`0x0A`) |
| Tap without strum | `tapWithoutStrumEnabled` (opcode `0x33`) |
| Sympathetic resonance | `resonate` + `sympatheticVelocityPercent` (opcode `0x56`) |
| Sustain / staccato | `sustainTable` per GM instrument; opcodes `0x01`, `0x09` |
| Metronome | `metronome.c`; BPM/beats/volume opcodes `0x4F`–`0x51` |
| Recording | `midi_recorder_*`; opcode `0x52` arms capture of `inputToUART` stream |
| Percussion mode | opcode `0x32` |

### 6. BLE-MIDI Control Plane and Telemetry (FIG. 4)

**GATT service** — UUID `03b80e5a-ede8-4b33-a751-6ce34ec4c700`; device name `Dijilele-XX` (last two bytes of MAC appended in `blemidi.c`).

**Inbound path** — `handleMidiMessage()` in `util.c`:
- Standard MIDI `0x80`–`0xBF` forwarded to UART/synth.
- Status `0xFE` with `len >= 2` treated as **private control frame**: `handleMessage(msg[0], msg[1])` — not MIDI Active Sensing.
- SysEx `0xF0` … `0xF7` accumulated for Diji protocols.

**Outbound telemetry** (`ble.c`) — On connect, `sendFirmwareVersion()` emits three 3-byte packets:
- `0x57, major, 0`
- `0x58, minor, 0`
- `0x59, patch, 0`

Companion app parses these for OTA prompts. `send_ble_settings_snapshot()` pushes current settings as SysEx JSON (base64).

**Representative opcodes** (`handleMessage`):

| Opcode | Function |
|--------|----------|
| `0x02` | Schedule strum calibration (deferred) |
| `0x16` | GM instrument program |
| `0x35` | Chords on/off |
| `0x5C` | Wi-Fi OTA server on/off |
| `0x52` | Record / play / pause / stop / loop |
| `0x53` | Select SPIFFS MIDI slot 1–10 |
| `0x57` | Strum-step enable / disable / reset index |
| `0x54` | Save/load settings JSON to SPIFFS |
| `0x5A`/`0x5B` | Strum velocity min/max |

### 7. SysEx MIDI File Upload Protocol (FIG. 4)

Proprietary manufacturer ID payload `0x7D 0x4D 0x49` (`process_diji_midi_upload_sysex`):

| Command byte | Action |
|--------------|--------|
| `0x01` BEGIN | Slot (1–10), 7-bit encoded total size; `malloc` buffer |
| `0x02` DATA | Slot, base64 chunk; `mbedtls_base64_decode` append |
| `0x03` END | Flush to `/spiffs/N.mid`, free session, re-parse if active file |

Max upload `DIJI_MIDI_UPLOAD_MAX`; static decode buffer avoids heap fragmentation on long BLE transfers. After overwrite, `diji_refresh_parsed_midi_if_path_matches()` re-parses for strum-step or sequencer.

### 8. Strum-Step Pedagogical Mode (FIG. 5)

**Enable** — `handleMessage(0x57, 1)` calls `midi_strum_step_set_enabled(true)`:
- Stops timed playback (`midiStop = true`).
- Parses current `midiFile` into sorted `MidiEvent` array (`midi_parse_current_file_events`).
- Resets `midi_step_event_index`.

**Per strum** — In `pic-midi.c` note path, `midi_strum_step_try_note()`:
- Skips if not in strum-step mode.
- Finds next `0x90` note-on with `velocity > 0`.
- Calls `strum_step_quiet_before_next()` (sustain CC off, previous note off).
- Sends note-on via `send_midi_event()`.
- Wraps index to file start at EOF.

**On release** — `midi_strum_step_on_strum_release()` sends note-off for sounding strum-step note.

**Disable** — `handleMessage(0x57, 0)` clears mode; timed `play_midi_file()` resumes.

Physical strum is the clock; `play_midi_file()` explicitly no-ops while strum-step active.

### 9. Wi-Fi Firmware OTA Pipeline (FIG. 6)

**Activation** — `wifiOn = true` from:
- BLE `handleMessage(0x5C, 1)`
- Physical button long-press (~5 s) in `main.c`

**Soft-AP** — `Wifi.cpp`: SSID/password configurable; `esp_wifi_set_mode(WIFI_MODE_AP)`; `WebServer` on port 80.

**HTTP API** (`WebServer.cpp`):
- `GET /api/debug` — JSON status (CORS headers via `http_ota_cors.h` for browser/WebView uploads)
- `POST /api/update` — multipart field `firmware` → `FirmwareUpdater.cpp`
- `POST /api/storage` — auxiliary storage upload
- Embedded `index.html` for standalone browser OTA

**Flash write** (`FirmwareUpdater.cpp`):
- Accumulates multipart headers across `recv()` boundaries (`header_accum`).
- Finds binary start with `findFirmwareStartInBuffer`.
- Writes in `OTA_WRITE_CHUNK` (128-byte) segments to avoid interrupt WDT reset.
- On success, schedules reboot (`OTA_RESTART_DELAY_MS`).

### 10. Settings Persistence

Runtime state serialized to cJSON (`build_runtime_settings_json`): instrument, tuning, transpose, vibrato, hammer-on %, strum velocity range, string enables, metronome, etc.

- **Save** — opcode `0x54, 1` writes `/spiffs/settings.json`.
- **Load** — opcode `0x54, 0` or boot `load_settings_at_boot()` applies via `handleMessage` for each field.
- **Snapshot** — on BLE connect, settings pushed as SysEx so companion UI syncs without polling each opcode.

### 11. MIDI Output Fan-Out

`inputToUART(byte1, byte2, byte3)` in `util.c`:
1. `midi_recorder_capture()` if recording armed.
2. `uartmidi_send_message()` to VS1103B (retries on semaphore contention).
3. `blemidi_send_message()` if BLE connected.
4. USB MIDI path via `usbmidi` component.

Ensures performed and sequenced notes reach all active sinks consistently.

---

## CLAIMS (DRAFT — FOR ATTORNEY REFINEMENT)

### Independent Claim 1 (Strum Detection Apparatus)

An electronic string instrument comprising:
a plurality of strum sensors each associated with a string;
a processor configured to execute a real-time sampling loop that maintains, per string, a dynamic rest center value and a sliding window of prior deflection samples;
wherein the processor is configured to compute a time derivative from a current deflection sample and samples in the sliding window;
wherein the processor is configured to detect a strum onset when the time derivative exceeds an onset threshold and a strum release when the time derivative falls below a release threshold;
wherein the processor is further configured to adjust the dynamic rest center value toward a current sensor reading when deflection is below a rest threshold while no strum is active;
and a MIDI output interface configured to emit note messages in response to detected strum onsets and releases.

### Independent Claim 2 (Strum-Step Method)

A method performed by firmware of an electronic string instrument, comprising:
receiving a MIDI file into non-volatile memory of the instrument;
parsing the MIDI file into an ordered list of note-on events;
entering a strum-step mode that disables time-based playback of the MIDI file;
upon detecting a strum gesture on a strum sensor, selecting a next note-on event from the ordered list, sending a sustain-off control message and a note-off for any previously sounding note, and transmitting the selected note-on event to a MIDI output;
upon detecting release of the strum gesture, transmitting a note-off for the selected note;
and wrapping to a beginning of the ordered list when the list is exhausted.

### Independent Claim 3 (Dual-Channel BLE-MIDI Control)

A method of configuring an electronic musical instrument, comprising:
establishing a Bluetooth Low Energy connection exposing a MIDI characteristic;
receiving, on the MIDI characteristic, packets including a status byte value 0xFE followed by an opcode byte and a data byte;
dispatching the opcode and data to an internal configuration handler that adjusts instrument behavior including at least one of: instrument preset, tuning system, strum velocity range, recording state, MIDI file slot selection, and strum-step mode;
and transmitting performance MIDI and telemetry packets on the same MIDI characteristic.

### Independent Claim 4 (Chunked SysEx MIDI Upload)

A method of loading a MIDI file into an electronic instrument, comprising:
receiving a system-exclusive begin message specifying a storage slot and total byte size;
allocating a buffer of the total byte size;
receiving a plurality of system-exclusive data messages each carrying a base64-encoded chunk;
decoding and appending each chunk to the buffer;
upon receiving a system-exclusive end message, writing the buffer to a file in flash memory at a path determined by the storage slot;
and re-parsing the file for at least one of sequencer playback and strum-step mode if the file is currently selected.

### Independent Claim 5 (BLE-Initiated Wi-Fi OTA)

An electronic musical instrument comprising:
a Bluetooth Low Energy interface;
a Wi-Fi interface configurable as a soft access point;
an HTTP server executing when the soft access point is active;
firmware stored in a rewritable flash partition;
wherein a control opcode received over the Bluetooth Low Energy interface causes the instrument to enable the soft access point and the HTTP server;
wherein the HTTP server accepts a POST request carrying a firmware binary and writes the binary into the rewritable flash partition for subsequent reboot.

### Dependent Claims (Examples)

6. The instrument of claim 1, wherein strum calibration is scheduled from a wireless callback and executed only in a dedicated sensor task that holds an analog-to-digital conversion mutex.

7. The method of claim 2, wherein detecting a strum gesture invokes `midi_strum_step_try_note` from a note-generation path shared with live performance sensing.

8. The method of claim 3, wherein firmware version major, minor, and patch components are telemetered using pseudo-status codes 0x57, 0x58, and 0x59 respectively.

9. The method of claim 4, wherein the storage slot maps to `/spiffs/N.mid` for N in 1 through 10.

10. The instrument of claim 5, wherein multipart HTTP upload headers are accumulated across multiple receive calls before locating a start of the firmware binary.

---

## REDUCTION TO PRACTICE / EVIDENCE

| Component | Path |
|-----------|------|
| Strum detection | `main/etar.c`, `main/etar.h` |
| Control dispatch | `main/util.c` (`handleMessage`, `handleMidiMessage`) |
| Strum-step engine | `main/midi_player.c`, `main/pic-midi.c` |
| BLE-MIDI GATT | `components/blemidi/blemidi.c` |
| Version telemetry | `main/ble.c`, `main/main.c` |
| SysEx MIDI upload | `main/util.c` (`process_diji_midi_upload_sysex`) |
| Wi-Fi / HTTP OTA | `main/Wifi.cpp`, `main/WebServer.cpp`, `main/FirmwareUpdater.cpp` |
| Pitch / fret mapping | `main/pic-midi.c` |
| Settings JSON | `main/settings.c`, `main/util.c` |
| Companion app | `diji_app_flutter/` (separate disclosure) |

---

## RELATIONSHIP TO COMPANION-APP DISCLOSURE

| Topic | Firmware (`dijiware-mac`) | App (`diji_app_flutter`) |
|-------|---------------------------|--------------------------|
| Strum-step | Parses MIDI, emits notes on strum | UI toggle, BLE opcode `0x57` |
| OTA | HTTP server, flash writer | Downloads .bin, joins Wi-Fi, POST upload |
| BLE control | `handleMessage` opcodes | `BleBridge.sendEtarControlMessage` |
| MIDI upload | SysEx receiver | WebView chunks over GATT |
| Version | Sends `0x57`/`0x58`/`0x59` | Parses telemetry, compares to dijilele.com |

Counsel may file combined system claims spanning both codebases or separate continuations.

---

## PRIOR ART SEARCH KEYWORDS

- Electronic stringed instrument MIDI
- Strum sensor derivative detection
- BLE-MIDI ESP32
- MIDI SysEx file transfer
- Guitar MIDI controller fret sensing
- Wi-Fi OTA multipart embedded HTTP
- Microtonal MIDI pitch bend
- MPE / per-string MIDI

---

## PUBLIC DISCLOSURE CHECKLIST

| Date | Disclosure |
|------|------------|
| [ ] | Shipping instruments with this firmware |
| [ ] | dijilele.com OTA binaries |
| [ ] | App Store app controlling device |
| [ ] | Demo / instructional videos |

---

## NEXT STEPS

- [ ] Patent attorney review (hardware + embedded software)
- [ ] Coordinate filing strategy with `PATENT_DISCLOSURE_DijiApp.md`
- [ ] Prior-art search on strum-derivative controllers
- [ ] Formal drawings from `docs/patent-figures/*.svg`
- [ ] Assign inventor contributions (firmware vs hardware PCB)

---

*End of disclosure draft.*
