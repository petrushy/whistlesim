"""attack_compare.py <rec.wav> <warbl.csv> <whistlecheck> <out_prefix> <preset>...

Finds the recorded attacks, renders each preset over the same spans with the measured breath (follow=,
breath=0), and prints recording and model attack measures side by side."""
import sys, os, subprocess, pickle
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import attacks as A

rec, csv, wc, prefix = sys.argv[1:5]
presets = sys.argv[5:]
rate, x = A.load_wav(rec)
pt, pp = A.load_csv(csv)
a, b, s0, s1 = A.align(x, rate, pt, pp)
def pfun(t):
    ms = a + b * t * 1000
    i = np.searchsorted(pt, ms); lo, hi = max(0, i - 1), min(len(pt) - 1, i)
    return pp[lo] if abs(pt[lo] - ms) < abs(pt[hi] - ms) else pp[hi]
ev, _, _ = A.find_events(x, rate)
recs = []
for t0, t1 in ev:
    r = A.measure(x, rate, t0, t1, pfun); r['t'] = t0; r['len'] = t1 - t0
    recs.append(r)

# Spans: from 0.3 s before the onset (not before the previous span's end) to 1.2 s after it.
NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
spans, prev = [], 0.0
for r in recs:
    m = int(round(69 + 12 * np.log2(r['f0'] / 440)))
    if m >= 74 + 12: m -= 12  # second octave: the low register's fingering
    low = f"{NOTES[m % 12]}{m // 12 - 1}"
    fr = max(prev + 0.01, r['t'] - 0.3); to = r['t'] + min(r['len'], 1.2)
    spans.append(f"{low}@{fr:.3f}-{to:.3f}"); prev = to
    r['span'] = (fr, to); r['fingered'] = low

results = {}
for p in presets:
    tag = ''.join(ch for ch in p if ch.isalnum())[:24]
    out = f"{prefix}-{tag}"
    cmd = ['nice', '-n', '19', wc, 'swell', rec, p, f'pressure={csv}', 'breath=0', 'follow=' + ','.join(spans), f'wav={out}']
    log = subprocess.run(cmd, capture_output=True, text=True).stdout
    open(out + '.log', 'w').write(log)
    mrate, mx = A.load_wav(out + '-follow.wav')
    ms = []
    for r in recs:
        fr, to = r['span']
        seg = mx[int(fr * mrate): int(to * mrate)]
        if len(seg) == 0 or np.abs(seg).max() < 1e-5: ms.append(None); continue
        env = 20 * np.log10(A.envelope(seg, mrate, 0.002))
        k = np.argmax(env > env.max() - 30)
        mt0 = fr + max(0, k - 5) * 0.002
        if to - mt0 < 0.3: ms.append(None); continue
        m = A.measure(mx, mrate, mt0, to, pfun)
        m['onset_vs_rec_ms'] = (mt0 + m['t10'] - (r['t'] + r['t10'])) * 1000
        ms.append(m)
    results[p] = ms
pickle.dump((recs, results), open(prefix + '.pkl', 'wb'))

def row(r, m=None):
    def f(v, fmt): return format(v, fmt) if v is not None and np.isfinite(v) else ' ' * len(format(0, fmt))
    s = (f"{r['note']:4} p{f(r.get('pa', np.nan), '5.0f')} br{f(r.get('breath_rise_ms', np.nan), '4.0f')} x{f(r.get('breath_peak', np.nan), '4.1f')} "
         f"| rise{f(r['rise_ms'], '4.0f')} over{f(r['overshoot_db'], '+5.1f')} set{f(r['settle_ms'], '4.0f')} "
         f"chirp{r['chirp']:4d}{r['chirp_to']:>4} chiff{f(r['chiff'], '6.1f')} early H2/H3 {f(r['h_early'][0], '+4.0f')}/{f(r['h_early'][1], '+4.0f')} steady {f(r['h_steady'][0], '+4.0f')}/{f(r['h_steady'][1], '+4.0f')}")
    return s
for p in presets:
    print(f"\n=== {p}")
    for r, m in zip(recs, results[p]):
        print(f"{r['t']:7.2f} REC {row(r)}")
        if m is None: print("        MOD (silent)")
        else: print(f"        MOD {row(m).split('|',1)[1]}  Pa@sound {m.get('pa_at_sound', np.nan):5.0f} (rec {r.get('pa_at_sound', np.nan):5.0f})  onset {m['onset_vs_rec_ms']:+5.0f} ms")
