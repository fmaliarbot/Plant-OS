# Plant OS — firmware V0.1

PlatformIO project for the ESP32-DevKitC-32E. One environment per milestone, so each piece of hardware is tested in isolation before it is combined with the rest.

| Environment | Tests | Hardware needed |
|---|---|---|
| `m1_soil` | 2 × DFRobot SEN0193 capacitive sensors (GPIO 34 and 35) | ESP32 + 2 sensors |
| `m2_environment` | BME280 (0x77) + BH1750 (0x23) over I2C (GPIO 21/22) | ESP32 + 2 STEMMA QT boards |
| `native` | Unit tests for the pure logic (filtering, calibration, validation) | Host only |

Every firmware drives GPIO 25 and 26 (pumps) LOW as the first instruction in `setup()`, even before the pumps are wired.

## 1. Host setup (once)

1. Install **Visual Studio Code**.
2. Extensions → **PlatformIO IDE** → Install. Restart VS Code.
3. USB driver: the DevKitC uses a Silicon Labs CP210x USB-UART bridge. Windows and macOS may need the "CP210x USB to UART Bridge VCP" driver from Silicon Labs. Linux does not.
4. *File → Open Folder* → this `firmware/` folder.

## 2. Unit tests (no hardware)

```
pio test -e native
```

Expected: `17 Tests 0 Failures`. Do not move on to hardware if anything fails. CI runs the same tests on every push.

## 3. Flash a milestone

```
pio run -e m1_soil -t upload
pio device monitor
```

If the upload hangs at `Connecting....___`, hold the **BOOT** button on the DevKitC until it starts writing.

Type commands in the monitor and press Enter.

### Milestone 1 — commands

| Command | Action |
|---|---|
| `d1` / `d2` | Capture the DRY point of sensor 1 (basil) / 2 (rosemary). Sensor in air, clean and dry. |
| `w1` / `w2` | Capture the WET point. Sensor in a glass of water **only up to the white line** — the electronics must stay dry. |
| `c` | Show stored calibration (survives reboots). |
| `x` | Clear calibration. |
| `s` | 60 s stability test. |
| `p` | Pause / resume streaming. |

CSV output: `t_ms,plant,mv,spread_mv,index,status`. `index` is a relative 0–100 scale between your dry and wet points. **It is not volumetric moisture %.**

Status values: `OK`, `UNCALIBRATED`, `LOW_IMPLAUSIBLE`, `HIGH_SATURATED`, `NOISY`. The future decision engine will only act on `OK`.

### Milestone 2 — commands

`i` rescans the I2C bus, `p` pauses. CSV output: `t_ms,temp_c,rh_pct,pressure_hpa,lux,bme_status,lux_status`. Status values: `OK`, `OUT_OF_RANGE`, `READ_ERROR`, `NOT_FOUND`.

## 4. Pass criteria

Numeric thresholds are **initial assumptions** and will be tuned with measured data.

**Milestone 1**
1. With no sensor connected, status is NOT `OK` (`HIGH_SATURATED`, `LOW_IMPLAUSIBLE` or `NOISY`). Proves validation works.
2. Sensor connected and in air: `mv` is stable (spread below ~50 mV).
3. Calibration: dry − wet ≥ 300 mV, otherwise it reports `INVALID`.
4. `s` with the sensor in moist, undisturbed soil: `STABLE` (stddev ≤ 15 mV).
5. Response: sensor in dry substrate, add water slowly → index rises within minutes.
6. Cross-check: moving or wetting only sensor 1 does not change sensor 2.

**Milestone 2**
1. Scan shows `0x23` and `0x77`, and the BME280 reports chip id `0x60` (a BMP280 reports a different id and has no humidity).
2. Temperature within about ±2 °C of a reference thermometer (estimate).
3. Covering the BH1750 drops lux near 0; a phone flashlight raises it sharply.
4. Unplugging the STEMMA QT cable switches status to `NOT_FOUND` / `READ_ERROR`; it recovers on reconnect.

## 5. Layout

```
include/pins.h           pin map (single source of truth)
include/pump_safety.h    pumps LOW at boot
lib/plantcore/           pure logic: filtering, calibration, validation, statistics
src/m1_soil/             Milestone 1 firmware
src/m2_environment/      Milestone 2 firmware
test/test_plantcore/     unit tests (Unity)
```
