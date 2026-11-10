#!/usr/bin/env python3
"""Searches for a voicing that makes a preset sound like a recorded whistle.

    tools/fit_voicing.py <recording> [name=value ...] [options]

    recording   goldie, generation, lofgren, lowd or moeck (see RECORDINGS below)
    name=value  where the search starts, for each voicing setting to search: any of
                edge (mm), bend (Pa), vortex (x the preset's vortex loss), gain, spread, slow, st, noise.
                They are passed to WhistleCheck as they are. Settings not named keep the preset's value.
    fixed=a,b   named settings that are passed on but not searched
    random=N    first try N random points around the start and continue from the best (default 0)
    box=S       how far the random points reach, in steps of each setting (default 3)
    sweeps=N    pattern-search sweeps after that (default 12; 0 only scores the start)
    threads=N   WhistleCheck runs at once (default 4)

Example:

    tools/fit_voicing.py goldie random=48 box=4 edge=0.05 bend=6 vortex=0.3 gain=1.1 spread=0 slow=0.45

The score is the mean size (dB) of the differences between the model's and the recording's octave averages of
H2-H8 (H6-H8 at half weight), over the notes both play, plus a little for the octaves' loudness difference. A
candidate must also stay playable, compared with the preset as it is:

  - `WhistleCheck transition` at three pressures: register jumps, notes ending in the wrong register, slow settling;
  - `WhistleCheck inside` for the lowest note and the sixth fingering: wherever the preset plays a clear note, the
    candidate must play the same one (a voicing can match the spectrum and need more breath to start).

Each of those it does worse than the preset costs 2 dB, and each note of the scale it loses 4 dB. Offline measures
have not predicted how a voicing plays, so finish with `WhistleCheck breath` and `glide`, and by playing it.

Runs WhistleCheck at low priority (nice 19). Build it first: cmake --build build --target WhistleCheck
(or set WHISTLECHECK to the binary, e.g. a copy, so that rebuilding doesn't change it under a running search).
"""

import concurrent.futures
import json
import math
import os
import random
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WHISTLECHECK = os.environ.get("WHISTLECHECK", os.path.join(REPO, "build/WhistleCheck_artefacts/Release/WhistleCheck"))

# Recording, the preset voiced after it, and the pressures (Pa) at which quick fingering changes are checked.
RECORDINGS = {
    "goldie": ("tools/Real-examples/Goldie-High-D-RODE.wav", "Goldie style", (400, 600, 800)),
    "generation": ("tools/Real-examples/Generation-High-D-RODE.wav", "Generation style", (400, 600, 800)),
    "lofgren": ("tools/Real-examples/Löfgren-High-D-RODE.wav", "Löfgren style", (400, 600, 800)),
    "lowd": ("tools/Real-examples/Goldie-Low-D-RODE.wav", "Low D whistle", (200, 300, 400)),
    "moeck": ("tools/Real-examples/Moeck-Soprano-RODE.wav", "Soprano recorder", (250, 300, 350)),
}

# Per setting: lowest, highest, first step.
BOUNDS = {
    "edge": (-0.3, 0.4, 0.05),
    "bend": (0.0, 40.0, 3.0),
    "vortex": (0.05, 3.0, 0.15),
    "gain": (0.5, 1.8, 0.1),
    "spread": (0.0, 3.0, 0.5),
    "slow": (0.0, 0.8, 0.1),
    "st": (0.006, 0.04, 0.001),
    "noise": (0.005, 0.15, 0.01),
}

HARMONIC_WEIGHTS = (1, 1, 1, 1, 0.5, 0.5, 0.5)  # H2..H8
OCTAVE_SPLIT = 1.94  # As WhistleCheck analyse: between the seventh and the octave
NUMBER = r"[+-]?\d+(?:\.\d+)?"


def whistlecheck(arguments):
    return subprocess.run(["nice", "-n", "19", WHISTLECHECK] + arguments, cwd=REPO, capture_output=True, text=True).stdout


def as_arguments(voicing):
    return ["%s=%g" % item for item in voicing.items()]


def parse_analysis(output):
    """The note tables of `analyse`: the recording's first, then the model's."""
    tables = []
    for line in output.splitlines():
        if line.strip().startswith("note "):
            tables.append([])
            continue
        row = re.match(r"\s+([A-G]#?\d)\s+[\d.]+\s+[\d.]+\s+([\d.]+)\s+[+-]?\d+\s+[\d.]+c\s+(-?[\d.]+)dB\s+\d+ms\s+\d+Hz\s+(.*)\|", line)
        if row and tables:
            # Wide values run together ("-87-124"), so take the numbers rather than split on spaces.
            tables[-1].append({"note": row.group(1), "hz": float(row.group(2)), "level": float(row.group(3)),
                               "harmonics": [float(x) for x in re.findall(NUMBER, row.group(4))]})
    return tables


def octave_averages(notes, shared):
    lowest = notes[0]["hz"]
    averages = []
    for low, high in ((0.97, OCTAVE_SPLIT), (OCTAVE_SPLIT, 2.0 * OCTAVE_SPLIT)):
        chosen = [n for n in notes if low * lowest <= n["hz"] < high * lowest and n["note"] in shared]
        if not chosen:
            averages.append(None)
            continue
        averages.append({"harmonics": [sum(n["harmonics"][k] for n in chosen) / len(chosen) for k in range(7)],
                         "level": sum(n["level"] for n in chosen) / len(chosen)})
    return averages


def speaking(preset, fingering, arguments):
    """`inside`: per pressure, the pitch (None if no clear note) and the level in dBFS."""
    rows = {}
    for line in whistlecheck(["inside", preset, str(fingering)] + arguments).splitlines():
        row = re.match(r"\s+(\d+)\s+([\d.]+)\s+(-?[\d.]+)\s", line)
        if row:
            rows[int(row.group(1))] = (float(row.group(2)), float(row.group(3)))
        elif re.match(r"\s+(\d+)\s+\(no clear note\)", line):
            rows[int(line.split()[0])] = (None, -200.0)
    return rows


_preset_speaking = {}


def preset_speaking(preset, fingering):
    if (preset, fingering) not in _preset_speaking:
        _preset_speaking[(preset, fingering)] = speaking(preset, fingering, [])
    return _preset_speaking[(preset, fingering)]


def evaluate(recording, voicing):
    """Returns (spectral score in dB, details); details hold the counts the penalties are made of."""
    wav, preset, pressures = RECORDINGS[recording]
    arguments = as_arguments(voicing)
    tables = parse_analysis(whistlecheck(["analyse", wav, preset] + arguments))
    if len(tables) < 2 or not tables[0] or not tables[1]:
        return 99.0, {"missing": 99, "bad": 99}
    recorded, model = tables[0], tables[1]
    shared = {n["note"] for n in recorded} & {n["note"] for n in model}
    details = {"missing": len({n["note"] for n in recorded} - shared)}

    theirs, mine = octave_averages(recorded, shared), octave_averages(model, shared)
    error = weight = 0.0
    for octave in range(2):
        if theirs[octave] is None or mine[octave] is None:
            error += 20.0
            continue
        differences = [m - t for m, t in zip(mine[octave]["harmonics"], theirs[octave]["harmonics"])]
        error += sum(w * abs(d) for w, d in zip(HARMONIC_WEIGHTS, differences))
        weight += sum(HARMONIC_WEIGHTS)
        details["octave %d, model - recording" % (octave + 1)] = [round(d, 1) for d in differences]
    score = error / max(weight, 1.0)
    if all(theirs) and all(mine):  # How much louder the second octave is, weakly
        loudness = (mine[1]["level"] - mine[0]["level"]) - (theirs[1]["level"] - theirs[0]["level"])
        details["octave loudness difference"] = round(loudness, 1)
        score += 0.1 * abs(loudness)

    bad = 0
    details["transitions"] = []
    for pressure in pressures:
        summary = re.search(r"(\d+) jump to another register.*?(\d+) end in the wrong one; settling median (\d+) ms, worst (\d+)",
                            whistlecheck(["transition", preset, "p=%d" % pressure] + arguments))
        if not summary:
            bad += 5
            continue
        jumps, stuck, median, worst = map(int, summary.groups())
        details["transitions"].append((pressure, jumps, stuck, median, worst))
        bad += jumps + stuck + (1 if median > 12 else 0)
    details["silent"] = []
    for fingering in (0, 5):
        played = speaking(preset, fingering, arguments)
        for pressure, (pitch, level) in preset_speaking(preset, fingering).items():
            if pitch is None or level < -40.0:
                continue
            mine_pitch, mine_level = played.get(pressure, (None, -200.0))
            if mine_pitch is None or mine_level < -45.0 or abs(1200.0 * math.log2(mine_pitch / pitch)) > 300.0:
                bad += 1
                details["silent"].append((fingering, pressure))
    details["bad"] = bad
    return score, details


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in RECORDINGS:
        sys.exit(__doc__)
    recording = sys.argv[1]
    voicing, fixed = {}, set()
    sweeps, randoms, box, threads = 12, 0, 3.0, 4
    for argument in sys.argv[2:]:
        name, _, value = argument.partition("=")
        if name == "fixed":
            fixed = set(value.split(","))
        elif name == "sweeps":
            sweeps = int(value)
        elif name == "random":
            randoms = int(value)
        elif name == "box":
            box = float(value)
        elif name == "threads":
            threads = int(value)
        elif name in BOUNDS:
            voicing[name] = float(value)
        else:
            sys.exit("Unknown setting or option: %s" % argument)

    def show(voicing):
        return " ".join(as_arguments(voicing))

    def clamp(name, value):
        return min(BOUNDS[name][1], max(BOUNDS[name][0], value))

    preset_score, preset_details = evaluate(recording, {})
    print("preset   %.2f  %s" % (preset_score, json.dumps(preset_details)), flush=True)

    def total(result):
        score, details = result
        return (score + 2.0 * max(0, details["bad"] - preset_details["bad"])
                + 4.0 * max(0, details["missing"] - preset_details["missing"]))

    best = evaluate(recording, voicing)
    print("start    %.2f  %s  %s" % (total(best), show(voicing), json.dumps(best[1])), flush=True)
    searched = [name for name in voicing if name not in fixed]

    with concurrent.futures.ThreadPoolExecutor(threads) as pool:
        if randoms > 0:
            random.seed(1)
            candidates = []
            for _ in range(randoms):
                candidate = dict(voicing)
                for name in searched:
                    candidate[name] = round(clamp(name, voicing[name] + random.uniform(-box, box) * BOUNDS[name][2]), 4)
                candidates.append(candidate)
            results = list(pool.map(lambda c: evaluate(recording, c), candidates))
            ranked = sorted(zip(candidates, results), key=lambda pair: total(pair[1]))
            for candidate, result in ranked[:5]:
                print("random   %.2f  %s" % (total(result), show(candidate)), flush=True)
            if total(ranked[0][1]) < total(best):
                voicing, best = ranked[0]

        # Pattern search: try each setting a step up and down, take the best move and whatever other settings'
        # moves still help on top of it; when nothing helps, halve the steps.
        steps = {name: BOUNDS[name][2] for name in searched}
        for sweep in range(sweeps):
            moves = []
            for name in searched:
                for direction in (1, -1):
                    moved = dict(voicing)
                    moved[name] = clamp(name, voicing[name] + direction * steps[name])
                    if moved[name] != voicing[name]:
                        moves.append((name, moved))
            results = list(pool.map(lambda move: evaluate(recording, move[1]), moves))
            ranked = sorted(zip(moves, results), key=lambda pair: total(pair[1]))
            if ranked and total(ranked[0][1]) < total(best) - 0.02:
                (name, voicing), best = ranked[0]
                taken = {name}
                for (other, moved), result in ranked[1:]:
                    if other in taken or total(result) >= total(best) + 0.5:
                        continue
                    combined = dict(voicing)
                    combined[other] = moved[other]
                    combined_result = evaluate(recording, combined)
                    if total(combined_result) < total(best) - 0.02:
                        voicing, best = combined, combined_result
                        taken.add(other)
            else:
                steps = {name: step * 0.5 for name, step in steps.items()}
            print("sweep %-2d %.2f  %s  %s" % (sweep + 1, total(best), show(voicing), json.dumps(best[1])), flush=True)
            if all(steps[name] < 0.2 * BOUNDS[name][2] for name in steps):
                break

    print("\nBest: %.2f (the preset as it is: %.2f)\n  WhistleCheck analyse %s \"%s\" %s"
          % (total(best), preset_score, RECORDINGS[recording][0], RECORDINGS[recording][1], show(voicing)))


if __name__ == "__main__":
    main()
