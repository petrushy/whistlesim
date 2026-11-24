# Notes on the WARBL 4.8 sensor stream

Points found while porting WhistleSim to the sensor stream in the WARBL2 4.8 beta (amowry/WARBL2 main at 709d15b, `warbl2_firmware/Sensor_stream.ino`) and its spec, `WARBL_sensor_stream_protocol_v3.md` (copied here as [WARBL_sensor_stream_protocol_v3.md](WARBL_sensor_stream_protocol_v3.md)). Gathered on 2026-11-21 to raise with Andrew Mowry.

**Tested on hardware (2026-11-21, USB, Honeywell sensor):** 4.8 streams to WhistleSim at about 500 frames/s with no lost frames and no hangs so far, and plays the same as the experimental firmware. The loop is 2 ms without a Bluetooth connection and 3 ms with one (point 4).

The wire layouts in the spec match the firmware source: the USB frame (39 bytes), the BLE batch (sizes as in the table), STREAM_INFO (62 bytes), the commands, and the renewal timeout.

**Status on 2026-11-24** (upstream main at 70edb66, firmware 4.8.1):

- Point 1 (covered thresholds): still open; the spec's section 9 still says "relative to baseline".
- Point 3: natural vibrato is in upstream (47eaa6e, on by default). The lower-holes pattern is proposed in amowry/WARBL2#45 and the Bluetooth on/off setting in #43 (Andrew prefers not to add it).
- Point 4: done. A USB stream now pauses Bluetooth, so the loop stays at 2 ms (amowry/WARBL2#44, merged; the spec copy here is updated to match). A Mac that is also paired over Bluetooth can still move the stream to Bluetooth; a fix is proposed in #46, and WhistleSim now sends its commands only to the USB port when WARBL is on a cable.
- Point 6, command versions: WhistleSim now sends only the official format (00 03 0F, version 3).

## 1. Spec: the covered thresholds are absolute readings, not relative to baseline

Spec section 9 says "Covered thresholds are relative to baseline" and gives `coverage = (rawHole - baseline[i]) / coveredThreshold[i]`. The firmware sends `toneholeCovered[i]` unchanged (`Sensor_stream.ino` line 256), and that is an absolute reading like the baseline. It is set during calibration by `toneholeCovered[i] = toneholeRead[i]`, and the baseline is subtracted from `toneholeRead` only when not calibrating (`Functions.ino` line 114: `if (calibration == 0)`). The firmware itself compares against `toneholeCovered[i] - toneholeBaseline[i]` (`Functions.ino` line 929). So the formula should be:

    coverage = (rawHole - baseline[i]) / (covered[i] - baseline[i])

Guard against covered ≤ baseline. A host that follows the spec's formula would read coverage several times too low.

## 2. Pressure scale: the spec's 2.184 counts/Pa looks right

The spec gives 2.184 counts/Pa (0.4579 Pa/count) for both sensors, and notes that the factor "should be confirmed".

- **Honeywell ABPLLND060MGAA3:** the nRF52 uses VDD as the ADC reference (`analogReference(AR_VDD4)`, `warbl2_firmware.ino` line 447), so the reading is ratiometric. The sensor spans 10–90 % of supply for 60 mbar (6000 Pa), and 0.8 × 16384 / 6000 = 2.184 counts/Pa. This agrees with the spec.
- **BMP585:** the firmware converts real pressure at 54.60 × 4 = 218.4 counts/hPa, which is 2.184 counts/Pa (`Functions.ino` line 139). This matches by construction.
- **Config Tool:** it converts with `(x - 100) * 24 / 900` inches of water at 10 bits (`webconfig/js/midi.js`). That is about 10 % less per count than the datasheet span. WhistleSim took its scale (0.4151 Pa/count) from this, so WhistleSim, not the spec, is probably the one that's off. WhistleSim keeps its scale for now, because its presets are fitted in it.

Worth asking: has the scale been checked against a known pressure, such as a water manometer?

## 3. Missing from 4.8: the features from Petrus' own firmware

Petrus' everyday firmware (petrushy/WARBL2, branch `own-release`) adds three things to 4.7. None of them is in 4.8:

- **"Tin whistle/flute, lower holes" fingering pattern** (`kModeWhistleLowerHoles`; CC 102 value 111): six-hole whistle fingering on L2–R4. An instrument preset set to this pattern sends no MIDI notes with firmware that doesn't know it.
- **Natural vibrato** (vibrato that fades with distance below the sounding hole; per-preset levels in the Config Tool).
- **A Bluetooth on/off setting.**

Without these, moving to the official firmware would break Petrus' normal MIDI playing; streaming isn't affected. Could they be merged upstream? PR #8 on petrushy/WARBL2 (`natural-vibrato`) holds the natural vibrato work.

## 4. Bluetooth stays on while streaming over USB, so the loop slows to 3 ms

When a BLE connection is open, 4.8 runs the main loop at 3 ms, also while streaming over USB (`README-integration.txt`: "Bluetooth is not disabled when USB streaming"; STREAM_INFO reports 3). The USB stream then runs at about 333 Hz instead of 500, and fingering and breath latency rise by about 1 ms.

Two things came out of the experimental firmware:

- A phone or tablet connecting over BLE interrupts the loop and the sensor readings, which made both the stream and the MIDI output erratic.
- Its fix: turn Bluetooth off (stop advertising, drop connections) while streaming over USB, and turn it back on when the stream stops or the USB host goes away.

Petrus' Bluetooth toggle (point 3) exists to keep the 2 ms loop. Suggestion: do the same in 4.8 when a USB stream starts, or offer a setting for it.

## 5. Orientation replaces raw accelerometer xyz and gyro

4.8 sends accel y and the fused roll, elevation and yaw (the Mahony filter), but no accel x/z and no gyro. This works for WhistleSim, which only uses elevation, for tilt. Two things to note:

- The elevation now comes from the firmware's fusion, where the app used to compute it from gravity (`atan2(ay, hypot(ax, az))`). The sign and zero may differ, so a host's tilt zero may need setting again.
- Hosts that want gravity-only tilt, or raw motion for gestures, can't compute it from accel y alone. A future version could carry accel x and z at little cost over USB, if anyone needs them.

## 6. Smaller points

- **The STREAM_INFO loop period is a prediction** (2 or 3 ms from `connIntvl`), as the spec says. It changes if BLE connects or disconnects during a stream, and no new INFO is sent when it does. Hosts should time frames by the device timestamps; WhistleSim does.
- **Over BLE, the INFO reply may be fragmented** (`sendSysExMessage`, unlike the batches). macOS reassembles it; worth testing on iOS and Windows.
- **Untested beta:** `README-integration.txt` says "not compiled or hardware tested", and the BMP mapping is provisional. With the experimental firmware, WARBL sometimes hung or was reset by the watchdog while streaming over USB (2026-11-21). That needs checking again on 4.8.
- **Command versions:** the firmware accepts command versions 1–3 with the registered ID, so the old command version number keeps working. The old manufacturer ID (7D 57) does not, so every host needs updating. (WhistleSim sent both formats until 2026-11-24.)
