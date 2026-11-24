"""Attack analysis of a recorded whistle take with its WARBL breath log.

attacks.py <rec.wav> <warbl.csv> [<model.wav> ...]
Model wavs (from WhistleCheck swell ... tune wav=) share the recording's timeline, so the same
events (found in the recording) are measured in them too.
"""
import sys, re, json
import numpy as np
import scipy.io.wavfile as wf
import scipy.signal as sg

def load_wav(path):
    rate, x = wf.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1: x = x[:, 0]
    if np.abs(x).max() > 2: x /= 2 ** 31 if np.abs(x).max() > 2 ** 24 else 2 ** 23
    return rate, x

def load_csv(path):
    ppc = 0.415148
    t, c = [], []
    hdr = None
    for line in open(path):
        if line.startswith('#'):
            m = re.search(r'pascal_per_count\s+([\d.]+)', line)
            if m: ppc = float(m.group(1))
            continue
        cells = line.strip().split(',')
        if hdr is None: hdr = cells; it = hdr.index('device_ms'); ip = hdr.index('pressure14'); continue
        if len(cells) <= ip: continue
        t.append(float(cells[it])); c.append(int(cells[ip]))
    t = np.array(t); c = np.array(c)
    vals, counts = np.unique(c, return_counts=True)
    rest = vals[counts.argmax()]
    return t, (c - rest) * ppc

def envelope(x, rate, hop_s):
    hop = int(rate * hop_s)
    n = len(x) // hop
    return np.sqrt(np.mean(x[:n * hop].reshape(n, hop) ** 2, axis=1) + 1e-20)

def align(x, rate, pt, pp):
    """WARBL ms = a + b * audio_s*1000, from envelope/pressure correlation at start and end (as WhistleCheck)."""
    env = envelope(x, rate, 0.01)
    p0 = pt[0]
    grid = np.zeros(int((pt[-1] - p0) / 10) + 1); cnt = np.zeros_like(grid)
    idx = ((pt - p0) / 10).astype(int)
    np.add.at(grid, idx, np.maximum(pp, 0)); np.add.at(cnt, idx, 1)
    grid = np.where(cnt > 0, grid / np.maximum(cnt, 1), 0)
    def best(fr, to):
        e = env[fr:to]
        bestl, bests = 0, -1
        for lag in range(-len(env), len(grid)):
            j0 = fr + lag
            lo, hi = max(0, -j0), min(len(e), len(grid) - j0)
            if hi - lo < len(e) * 9 // 10: continue  # 90 % of the window within the log, as WhistleCheck
            ee = e[lo:hi]; g = grid[j0 + lo:j0 + hi]
            s = np.dot(ee, g) / (np.linalg.norm(ee) * np.linalg.norm(g) + 1e-20)
            if s > bests: bests, bestl = s, lag
        return bestl, bests
    w = min(len(env), 2000)
    l0, s0 = best(0, w); l1, s1 = best(len(env) - w, len(env))
    i0, i1 = w / 2, len(env) - w / 2
    b = 1 + (l1 - l0) / max(1, i1 - i0)
    a = p0 + 10 * (l0 - (b - 1) * i0)
    return a, b, s0, s1

def pitch(seg, rate, fmin=500, fmax=2400):
    """Autocorrelation pitch with parabolic refinement; returns (hz, clarity)."""
    seg = seg - seg.mean()
    if len(seg) < rate / fmin * 3: return 0.0, 0.0
    r = sg.fftconvolve(seg, seg[::-1], mode='full')[len(seg) - 1:]
    r /= r[0] + 1e-20
    lo, hi = int(rate / fmax), int(rate / fmin)
    k = lo + np.argmax(r[lo:hi])
    # prefer the shortest lag within 0.9 of the best peak (avoid octave-down errors)
    peaks = [i for i in range(lo + 1, hi - 1) if r[i] > r[i - 1] and r[i] >= r[i + 1] and r[i] > 0.9 * r[k]]
    if peaks: k = peaks[0]
    if 1 <= k < len(r) - 1:
        a, b_, c = r[k - 1], r[k], r[k + 1]
        d = 0.5 * (a - c) / (a - 2 * b_ + c) if (a - 2 * b_ + c) != 0 else 0
    else: d = 0
    return rate / (k + d), r[k]

NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
def name(hz):
    m = 69 + 12 * np.log2(hz / 440); n = int(round(m))
    return f"{NOTES[n % 12]}{n // 12 - 1}", (m - n) * 100

def harmonics(seg, rate, f0, kmax=6):
    w = np.hanning(len(seg)); X = np.abs(np.fft.rfft(seg * w)); fr = np.fft.rfftfreq(len(seg), 1 / rate)
    out = []
    for k in range(1, kmax + 1):
        band = (fr > k * f0 * 0.97) & (fr < k * f0 * 1.03)
        out.append(X[band].max() if band.any() else 1e-12)
    out = np.array(out)
    return 20 * np.log10(out[1:] / (out[0] + 1e-20) + 1e-12)

def hf_noise(seg, rate, f0):
    """Energy 2-8 kHz between harmonics relative to the fundamental (dB): chiff/breath."""
    w = np.hanning(len(seg)); X = np.abs(np.fft.rfft(seg * w)) ** 2; fr = np.fft.rfftfreq(len(seg), 1 / rate)
    h = np.abs(fr / f0 - np.round(fr / f0)) * f0
    between = (fr > 2000) & (fr < 8000) & (h > max(60.0, 0.25 * f0))
    fund = X[(fr > f0 * 0.95) & (fr < f0 * 1.05)].sum()
    return 10 * np.log10(np.median(X[between]) * (f0 / (fr[1] - fr[0])) / (fund + 1e-30) + 1e-30) if between.any() else np.nan

def find_events(x, rate, min_gap=0.15, min_len=0.25):
    env = 20 * np.log10(envelope(x, rate, 0.005))
    top = np.percentile(env, 99)
    on = env > top - 40
    # close short gaps, then find runs
    ev = []
    i, n = 0, len(on)
    while i < n:
        if on[i]:
            j = i
            gap = 0
            while j < n and (on[j] or gap < int(min_gap / 0.005)):
                gap = 0 if on[j] else gap + 1
                j += 1
            j -= gap
            if (j - i) * 0.005 >= min_len: ev.append((i * 0.005, j * 0.005))
            i = j + 1
        else: i += 1
    return ev, env, top

def measure(x, rate, t0, t1, pfun, f_expect=None):
    """One attack starting near t0 (sound onset) and lasting to t1."""
    r = {}
    hop = 0.002
    env = envelope(x[int(t0 * rate) - int(0.05 * rate): int(min(t1, t0 + 1.0) * rate)], rate, hop)
    tt = -0.05 + np.arange(len(env)) * hop
    # steady: 300-600 ms after onset (or the middle of a short note)
    s0, s1 = t0 + min(0.3, (t1 - t0) * 0.4), t0 + min(0.6, (t1 - t0) * 0.8)
    steady = x[int(s0 * rate): int(s1 * rate)]
    srms = np.sqrt(np.mean(steady ** 2))
    f0, cl = pitch(steady[:int(0.1 * rate)], rate)
    if f_expect: f0 = f_expect
    r['f0'] = f0; r['level'] = 20 * np.log10(srms + 1e-20)
    # 10/90 % of steady RMS
    def reach(share):
        k = np.argmax(env >= share * srms)
        return tt[k] if env[k] >= share * srms else np.nan
    r['t10'] = reach(0.1); r['t90'] = reach(0.9)
    r['rise_ms'] = (r['t90'] - r['t10']) * 1000
    # overshoot: peak in first 200 ms vs steady
    first = env[(tt >= 0) & (tt < 0.2)]
    r['overshoot_db'] = 20 * np.log10(first.max() / srms) if len(first) else np.nan
    # pitch track every 5 ms (window 20 ms) over the first 300 ms after 10 % point
    start = t0 + (r['t10'] if np.isfinite(r['t10']) else 0)
    track = []
    for k in range(60):
        a = start + k * 0.005
        seg = x[int(a * rate): int((a + 0.02) * rate)]
        hz, c = pitch(seg, rate)
        track.append((k * 5, hz, c))
    sname, scents = name(f0) if f0 > 0 else ('?', 0)
    r['note'] = sname; r['cents'] = scents
    # settle: first time after which pitch stays within 15 c of steady (clarity > .7)
    devs = [(ms, 1200 * np.log2(hz / f0) if hz > 0 and c > 0.7 else np.nan) for ms, hz, c in track]
    settle = np.nan
    for i in range(len(devs)):
        rest = [d for _, d in devs[i:]]
        if all(np.isfinite(d) and abs(d) < 15 for d in rest[:20]):
            settle = devs[i][0]; break
    r['settle_ms'] = settle
    early = [d for ms, d in devs[:8] if np.isfinite(d)]
    r['early_cents'] = float(np.median(early)) if early else np.nan
    # other register early: any 20 ms frame in the first 150 ms near 2x or 0.5x / 1.5x
    other = [d for ms, d in devs[:30] if np.isfinite(d) and abs(d) > 500]
    r['chirp'] = len(other) * 5  # ms of other-register frames
    r['chirp_to'] = (name(f0 * 2 ** (np.median(other) / 1200))[0] if other else '')
    # harmonics early (20-60 ms after 10 %) vs steady
    e = x[int((start + 0.02) * rate): int((start + 0.06) * rate)]
    r['h_early'] = harmonics(e, rate, f0) if len(e) > 100 else np.full(5, np.nan)
    r['h_steady'] = harmonics(steady, rate, f0)
    r['chiff'] = hf_noise(x[int(start * rate): int((start + 0.03) * rate)], rate, f0)
    r['noise_steady'] = hf_noise(steady, rate, f0)
    if pfun is not None:
        # breath: from 300 ms before onset to 400 ms after
        ts = np.arange(t0 - 0.4, t0 + 0.4, 0.002)
        p = np.array([pfun(t) for t in ts])
        plat = np.nanmedian([pfun(t) for t in np.arange(s0, s1, 0.01)])
        r['pa'] = plat
        # breath start: last time before onset that p < 10 % of plateau; 90 % time after
        below = np.where((ts < t0 + 0.05) & (p < 0.1 * plat))[0]
        b10 = ts[below[-1]] if len(below) else np.nan
        above = np.where((ts > b10) & (p >= 0.9 * plat))[0] if np.isfinite(b10) else []
        b90 = ts[above[0]] if len(above) else np.nan
        r['breath_rise_ms'] = (b90 - b10) * 1000
        r['breath_lead_ms'] = (t0 + r['t10'] - b10) * 1000 if np.isfinite(r['t10']) else np.nan  # breath start -> 10 % sound
        r['pa_at_sound'] = pfun(t0 + r['t10']) if np.isfinite(r['t10']) else np.nan
        pk = p[(ts > b10) & (ts < t0 + 0.2)] if np.isfinite(b10) else p
        r['breath_peak'] = (pk.max() / plat) if len(pk) and plat > 0 else np.nan
    return r

def main():
    rec, csv = sys.argv[1], sys.argv[2]
    models = sys.argv[3:]
    rate, x = load_wav(rec)
    pt, pp = load_csv(csv)
    a, b, s0, s1 = align(x, rate, pt, pp)
    print(f"# aligned: match {s0:.2f} start, {s1:.2f} end, clocks {(b - 1) * 1e6:+.0f} ppm", file=sys.stderr)
    # 2 ms nearest-sample pressure (no smoothing), and a 20 ms mean as WhistleCheck uses
    def pfun(t):
        ms = a + b * t * 1000
        i = np.searchsorted(pt, ms)
        lo, hi = max(0, i - 1), min(len(pt) - 1, i)
        return pp[lo] if abs(pt[lo] - ms) < abs(pt[hi] - ms) else pp[hi]
    ev, env, top = find_events(x, rate)
    out = []
    for (t0, t1) in ev:
        # refine onset: first 2 ms frame within 30 dB of the event's own steady
        r = measure(x, rate, t0, t1, pfun)
        r['t'] = t0; r['len'] = t1 - t0
        mods = []
        for mw in models:
            mrate, mx = load_wav(mw) if isinstance(mw, str) else mw
            # model onset: search near the recording's onset
            menv = 20 * np.log10(envelope(mx[int(max(0, t0 - 0.3) * mrate): int((t0 + 0.6) * mrate)], mrate, 0.005))
            mtop = np.percentile(menv, 95)
            k = np.argmax(menv > mtop - 40)
            mt0 = max(0, t0 - 0.3) + k * 0.005
            if menv.max() < -70: mods.append(None); continue
            mods.append(measure(mx, mrate, mt0, t1, None, None))
        r['models'] = mods
        out.append(r)
    return out

if __name__ == '__main__':
    import pickle
    res = main()
    pickle.dump(res, open(sys.argv[0].replace('.py', '.pkl'), 'wb'))
    for r in res:
        print(f"{r['t']:7.2f} {r['len']:5.2f} {r['note']:4} {r['cents']:+4.0f}  Pa {r.get('pa', np.nan):5.0f}  breath rise {r.get('breath_rise_ms', np.nan):5.0f} ms  lead {r.get('breath_lead_ms', np.nan):5.0f}  "
              f"Pa@sound {r.get('pa_at_sound', np.nan):5.0f}  peak x{r.get('breath_peak', np.nan):4.2f} | rise {r['rise_ms']:4.0f} ms  over {r['overshoot_db']:+4.1f} dB  settle {r['settle_ms']:4.0f}  early {r['early_cents']:+4.0f} c  chirp {r['chirp']:3d}{r['chirp_to']:>4}  chiff {r['chiff']:5.1f} steady {r['noise_steady']:5.1f}")
