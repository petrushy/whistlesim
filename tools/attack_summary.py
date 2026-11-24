"""attack_summary.py <prefix>.pkl: the per-attack table and medians by kind (soft or sharp breath onset,
octave) from attack_compare.py."""
import pickle, sys, numpy as np
recs, res = pickle.load(open(sys.argv[1], 'rb'))
def kind(r):
    return 'soft' if r.get('breath_rise_ms', 0) > 40 else 'hard'
def med(v):
    v = [x for x in v if x is not None and np.isfinite(x)]
    return np.median(v) if v else np.nan
print("Per attack (s = soft breath onset, h = sharp/tongued):")
print(f"{'t':>6} {'note':4} k {'Pa':>5} {'peak':>4} | {'REC rise':>8} {'over':>5} {'chirp':>8} {'chiff':>5} | " + " | ".join(f"{p[:14]:>14} rise over chirp chiff late" for p in res))
for i, r in enumerate(recs):
    line = f"{r['t']:6.1f} {r['note']:4} {kind(r)[0]} {r.get('pa', np.nan):5.0f} x{r.get('breath_peak', np.nan):3.1f} | {r['rise_ms']:8.0f} {r['overshoot_db']:+5.1f} {r['chirp']:4d}{r['chirp_to']:>4} {r['chiff']:5.0f} |"
    for p, ms in res.items():
        m = ms[i]
        if m is None: line += f" {'(silent)':>36} |"; continue
        late = m['onset_vs_rec_ms'] + (r['breath_lead_ms'] * -1 if np.isfinite(r.get('breath_lead_ms', np.nan)) else 0)
        line += f" {'':14} {m['rise_ms']:4.0f} {m['overshoot_db']:+4.1f} {m['chirp']:3d}{m['chirp_to']:>4} {m['chiff']:5.0f} {m['onset_vs_rec_ms']:+4.0f} |"
    print(line)
print()
groups = {'octave 1 soft': lambda r: r['f0'] < 1150 and kind(r) == 'soft', 'octave 1 hard': lambda r: r['f0'] < 1150 and kind(r) == 'hard',
          'octave 2 hard': lambda r: r['f0'] >= 1150 and kind(r) == 'hard'}
for g, sel in groups.items():
    idx = [i for i, r in enumerate(recs) if sel(r)]
    rr = [recs[i] for i in idx]
    print(f"{g} ({len(idx)}): REC  rise {med([r['rise_ms'] for r in rr]):4.0f} ms  overshoot {med([r['overshoot_db'] for r in rr]):+4.1f} dB  settle {med([r['settle_ms'] for r in rr]):4.0f} ms"
          f"  chirps {sum(r['chirp'] >= 15 for r in rr)}/{len(rr)}  chiff {med([r['chiff'] for r in rr]):5.1f} dB  sound from breath start {med([r['breath_lead_ms'] for r in rr]):+4.0f} ms  Pa@10% {med([r['pa_at_sound'] for r in rr]):4.0f}  breath peak x{med([r['breath_peak'] for r in rr]):.1f}")
    for p, ms in res.items():
        mm = [ms[i] for i in idx if ms[i] is not None]
        lead = [ms[i]['onset_vs_rec_ms'] + recs[i]['breath_lead_ms'] for i in idx if ms[i] is not None]
        up = sum(1 for m in mm if m['chirp'] >= 15 and m['chirp_to'] and m['chirp_to'][-1] > m['note'][-1])
        down = sum(1 for m in mm if m['chirp'] >= 15 and m['chirp_to'] and m['chirp_to'][-1] < m['note'][-1])
        print(f"   {p[:38]:38}  rise {med([m['rise_ms'] for m in mm]):4.0f} ms  overshoot {med([m['overshoot_db'] for m in mm]):+4.1f} dB  settle {med([m['settle_ms'] for m in mm]):4.0f} ms"
              f"  chirps up {up} down {down}  chiff {med([m['chiff'] for m in mm]):5.1f} dB  sound from breath start {med(lead):+4.0f} ms  Pa@10% {med([m['pa_at_sound'] for m in mm]):4.0f}")
