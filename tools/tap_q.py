#!/usr/bin/env python3
# Usage: python tools/tap_q.py <finger-taps.wav>  (needs numpy and scipy)
# Ring-down Q of finger taps: each tap's first and second resonance, from the decay of a band-passed envelope.
import sys, numpy as np, scipy.io.wavfile as w, scipy.signal as sg
r, x = w.read(sys.argv[1]); x = x.astype(float)
if x.ndim > 1: x = x[:, 0]
model = {'D': (585,1181), 'E': (663,1336), 'F#': (744,1500), 'G': (793,1598), 'A': (901,1801), 'B': (1012,2028), 'C#': (1138,2263)}
env = np.abs(sg.hilbert(sg.sosfilt(sg.butter(4, [400, 3000], 'band', fs=r, output='sos'), x)))
env = sg.sosfiltfilt(sg.butter(2, 200, fs=r, output='sos'), env)
peaks, _ = sg.find_peaks(env, height=env.max() * 0.08, distance=int(0.35 * r))
def ringQ(seg, f):
    sos = sg.butter(2, [f * 0.93, f * 1.07], 'band', fs=r, output='sos')
    e = np.abs(sg.hilbert(sg.sosfilt(sos, seg)))
    db = 20 * np.log10(e / e.max() + 1e-12)
    i0 = int(np.argmax(e))
    after = db[i0:]
    try:
        a = i0 + np.where(after < -4)[0][0]; b = i0 + np.where(after < -24)[0][0]
    except IndexError:
        return None, None
    t = np.arange(a, b) / r
    slope = np.polyfit(t, db[a:b], 1)[0]          # dB/s
    tau = -20 / np.log(10) / slope                 # amplitude time constant
    return np.pi * f * tau, (b - a) / r
print("tap   time  note  f1 Hz    Q1   | f2 Hz    Q2")
for k, p in enumerate(peaks):
    seg = x[max(0, p - int(0.01 * r)): p + int(0.45 * r)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 17)); fr = np.fft.rfftfreq(1 << 17, 1 / r)
    band = (fr > 520) & (fr < 1250)
    f1 = fr[band][np.argmax(spec[band])]
    note = min(model, key=lambda n: abs(np.log(f1 / model[n][0])))
    q1, d1 = ringQ(seg, f1)
    f2m = model[note][1]; band2 = (fr > f2m * 0.93) & (fr < f2m * 1.07)
    f2 = fr[band2][np.argmax(spec[band2])]
    q2, d2 = ringQ(seg, f2)
    fmt = lambda q: f"{q:5.0f}" if q else "    -"
    print(f"{k:3d} {p / r:6.2f}  {note:3s} {f1:6.0f} {fmt(q1)}   | {f2:6.0f} {fmt(q2)}")
