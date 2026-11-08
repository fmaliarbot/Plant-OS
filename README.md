# Plant OS

An operating system for plants: per-plant sensing, species-aware decisions and individual automatic watering.

**Current stage:** V0.1 functional prototype — two plants (basil and rosemary), one shared tank, two independent peristaltic pumps, ESP32 controller. Business case and market work are deliberately out of scope for now.

## Repository layout

| Path | Contents |
|---|---|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO, C++/Arduino). One environment per milestone + host unit tests. |
| [`docs/continuity/`](docs/continuity/) | Project continuity documents (in Spanish): decisions, BOM, wiring, hydraulics, assembly guide. |
| `.github/workflows/` | CI: unit tests + ESP32 builds on every push that touches `firmware/`. |

Planned (not started): `backend/` (Python + FastAPI + MQTT + SQLite) and `dashboard/` (React + Recharts).

## Milestones

| # | Milestone | Status |
|---|---|---|
| 1 | Soil moisture sensors (2 × DFRobot SEN0193) | Firmware + tests ready, hardware pending |
| 2 | Environment sensors (BME280 + BH1750) | Firmware ready, hardware pending |
| 3 | Hydraulics: tank, pumps, flow calibration, leak test | Not started |
| 4 | Firmware: non-blocking control, communication, safety | Not started |
| 5 | Backend: FastAPI, MQTT, SQLite, decision engine, plant registry | Not started |
| 6 | Dashboard | Not started |
| 7 | Automation: species profiles, calibrated thresholds, limits, alerts | Not started |
| 8 | Real-world test (≥ 8 weeks) | Not started |

Rule: nothing gets automated until every sensor and actuator has passed its individual test.

## Hardware (V0.1)

| Part | Model |
|---|---|
| Controller | Espressif ESP32-DevKitC-32E |
| Soil moisture | DFRobot SEN0193 × 2 |
| Temperature / humidity / pressure | Adafruit BME280 (2652) |
| Light | Adafruit BH1750 (4681) |
| Pump drivers | Adafruit MOSFET Driver (5648) × 2 |
| Pumps | Kamoer NKP-DC-S10B 12 V × 2 |
| Pump PSU | Mean Well GST25E12-P1J (12 V, 2.08 A) |
| Tank level (Milestone 3) | DFRobot SEN0204 |

Layout: single level, about 33 × 25 cm footprint and 35–45 cm tall with plants — 12 cm pots in front, a low 1.5–2 L tank behind them with both pumps on its lid, and the electronics box on the side. Pump outlets sit at least 3 cm above the tank's max water level, so no siphon or check valves are needed.

Full BOM, compatibility checks and diagrams: see `docs/continuity/`.

## Pin map

| GPIO | Function |
|---|---|
| 25 | Basil pump driver (In) |
| 26 | Rosemary pump driver (In) |
| 34 | Basil soil sensor (ADC1) |
| 35 | Rosemary soil sensor (ADC1) |
| 21 / 22 | I2C SDA / SCL |
| 27 | Tank level (reserved, Milestone 3) |

## Conventions

- Code, comments, identifiers, serial output and commit messages are in **English**.
- Project documents in `docs/` are in Spanish.
- Every spec in the docs is tagged **[Confirmado]** (datasheet/vendor), **[Estimación]** (calculated/typical, must be measured) or **[Supuesto]** (engineering assumption, to be revisited with data).
