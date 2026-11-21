#!/usr/bin/env python3
"""Turn on the WARBL2 sensor stream over USB MIDI and print the decoded frames.

A quick hardware check for the sensor stream: WARBL firmware 4.8 and later (protocol version 3, WARBL's registered
ID 00 03 0F; docs/WARBL_sensor_stream_protocol_v3.md), or the earlier experimental firmware (7D 57, versions 1-2).
Needs: pip install mido python-rtmidi

    python3 stream_monitor.py              # stream with normal MIDI output still on
    python3 stream_monitor.py --suppress   # stream with note/pitchbend/CC output off
"""

import argparse
import sys
import time

import mido

WARBL_ID = [0x00, 0x03, 0x0F]  # Official firmware; sends version 3
LEGACY_ID = [0x7D, 0x57]       # Experimental firmware; sends versions 1-2, accepts commands as version 1
FRAME, INFO, CONFIG, REQUEST_INFO = 0x01, 0x10, 0x20, 0x21
ANGLE_SCALE = 32767 / 180  # Version 3 angles: counts per degree


def frame_fields(version):
    """Version 3 sends accel y and three angles; earlier versions accel and gyro xyz. Version 1 has 12-bit pressure."""
    motion = ([("ay", 16), ("roll", 16), ("elevation", 16), ("yaw", 16)] if version >= 3 else
              [("ax", 16), ("ay", 16), ("az", 16), ("gx", 16), ("gy", 16), ("gz", 16)])
    return ([("seq", 14), ("time", 21)] + [(f"hole{i}", 10) for i in range(9)]
            + [("pressure", 14 if version >= 2 else 12)] + motion + [("covered", 9), ("flags", 7)])


SIGNED = {"ax", "ay", "az", "gx", "gy", "gz", "roll", "elevation", "yaw"}


def unpack(data, widths):
    """Read MSB-first values from 7-bit SysEx data bytes."""
    values, pos = [], 0
    for bits in widths:
        v = 0
        for _ in range(bits):
            v = (v << 1) | ((data[pos // 7] >> (6 - pos % 7)) & 1)
            pos += 1
        values.append(v)
    return values


def decode_frame(data, version):
    fields = frame_fields(version)
    frame = dict(zip([n for n, _ in fields], unpack(data, [w for _, w in fields])))
    if version < 2:
        frame["pressure"] <<= 2  # Shown at 14 bits either way
    for name in SIGNED & frame.keys():
        if frame[name] >= 0x8000:
            frame[name] -= 0x10000
    return frame


def parse_header(data):
    """(version, type, payload) of a WARBL stream message, or None."""
    data = list(data)
    if data[:3] == WARBL_ID and len(data) >= 5 and data[3] >= 3:
        return data[3], data[4], data[5:]
    if data[:2] == LEGACY_ID and len(data) >= 4 and data[2] in (1, 2):
        return data[2], data[3], data[4:]
    return None


def find_warbl(names):
    for name in names:
        if "WARBL" in name.upper():
            return name
    sys.exit(f"No WARBL MIDI port found. Ports: {names}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suppress", action="store_true", help="turn off note/pitchbend/CC output while streaming")
    args = parser.parse_args()

    out = mido.open_output(find_warbl(mido.get_output_names()))
    inp = mido.open_input(find_warbl(mido.get_input_names()))

    def send(type_, payload=()):
        # Both formats: each firmware ignores the other's.
        out.send(mido.Message("sysex", data=WARBL_ID + [3, type_] + list(payload)))
        out.send(mido.Message("sysex", data=LEGACY_ID + [1, type_] + list(payload)))

    config = [0x01 | (0x02 if args.suppress else 0) | 0x04]  # 0x04: keep-alive (the experimental firmware needs it)
    send(CONFIG, config)
    send(REQUEST_INFO)

    last_seq, frames, gaps, start = None, 0, 0, time.monotonic()
    last_renewal = time.monotonic()
    try:
        for msg in inp:
            # WARBL stops a stream that isn't renewed within 3 s (so a killed monitor can't leave it streaming).
            if time.monotonic() - last_renewal > 1.0:
                send(CONFIG, config)
                last_renewal = time.monotonic()
            header = parse_header(msg.data) if msg.type == "sysex" else None
            if header is None:
                continue
            version, kind, payload = header
            if kind == INFO:
                v = unpack(payload, [16] * (24 if version >= 2 else 23))
                ambient = v[23] if version >= 2 else v[19] << 4
                print(f"protocol {version}  firmware {v[0]}  baselines {v[1:10]}  covered {v[10:19]}  ambient {ambient} (14-bit)  "
                      f"accel range {v[20]}  gyro range {v[21]}  loop {v[22]} ms")
            elif kind == FRAME:
                f = decode_frame(payload, version)
                if last_seq is not None and f["seq"] != (last_seq + 1) & 0x3FFF:
                    gaps += 1
                last_seq = f["seq"]
                frames += 1
                if frames % 50 == 0:  # Print about 10 times a second.
                    rate = frames / (time.monotonic() - start)
                    holes = " ".join(f"{f[f'hole{i}']:4d}" for i in range(9))
                    if version >= 3:
                        motion = (f"acc y {f['ay']:6d}  roll {f['roll'] / ANGLE_SCALE:7.1f}  "
                                  f"elev {f['elevation'] / ANGLE_SCALE:6.1f}  yaw {f['yaw'] / ANGLE_SCALE:7.1f}")
                    else:
                        motion = (f"acc {f['ax']:6d} {f['ay']:6d} {f['az']:6d}  "
                                  f"gyro {f['gx']:6d} {f['gy']:6d} {f['gz']:6d}")
                    print(f"{rate:6.0f} fps  gaps {gaps}  p {f['pressure']:5d}  holes {holes}  "
                          f"covered {f['covered']:09b}  {motion}")
    except KeyboardInterrupt:
        pass
    finally:
        send(CONFIG, [0])  # Stream off, normal MIDI output back on.


if __name__ == "__main__":
    main()
