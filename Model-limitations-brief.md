# WhistleSim: the physical model against recorded whistles, and its limitations

A brief for a review of whether WhistleSim's physical model of a fipple flute can be improved. It covers what the model computes, how it compares with five studio recordings of real instruments (made 2026-11-10), which differences look like real physics gaps and which look like measurement noise, what has already been tried, and the constraints any change must keep.

Repository: `whistlesim` (JUCE/CMake, C++). The model is in `Source/Whistle/`:

- `WhistleModel.cpp` / `.h`: the time-domain model. `tick()` (from line ~398) is one internal sample.
- `WhistleGeometry.cpp`: the presets' dimensions (`presets()`) and voicings (`presetVoicing()`).
- `Acoustics.cpp`: a frequency-domain transfer-matrix model of the same bore, used by the tuner and as a cross-check.
- `JetStability.cpp`: a Rayleigh-equation solver for the jet leaving the windway.

The offline analysis tool is `tools/WhistleCheck.cpp`. The recordings are in `tools/Real-examples/`. `README.md`, sections "Whistle model" and "Comparing with real whistles", has the history of earlier findings.

---

## 1. What the model computes

Everything runs at about 96 kHz internally (twice the device rate below 88.2 kHz, then decimated with a 63-tap FIR). It runs in real time on the audio thread, with no allocation and no locks.

### 1.1 Breath to jet speed

Mouth pressure p, in Pa, comes from the WARBL breath sensor through a response curve. The air in the windway (length L) is accelerated as follows:

    ρ L dU/dt = p − ½ ρ U²          (implicit solve per sample; time constant L/U ≈ 0.3–1 ms)

Windway friction is modelled as Blasius boundary layers, with displacement thickness 1.72 √(νL/U) on each wall, capped at h/6. They reduce the flux to a share `flowShare` of 67–85 % of the core flow. The jet's centre-line speed stays U.

### 1.2 Jet: receptivity, convection, growth, band-pass

- **Input:** the acoustic velocity through the window, `v = Q_window / A_window`.
- **Travel time:** v is delayed by the jet's travel time over the window length W, `τ = W / (c_conv · U)`, with `c_conv = jetConvection = 0.6`.
- **Band-pass:** v then passes through 1–2 identical biquad band-pass stages (`jetStages`), with unity gain and zero phase at the centre. The centre is at `f_c = St · U / h` (`jetStrouhal`, about 0.02, or 0.017 for the Goldie and 0.0095 for the low D) and Q = 0.7. The filter is retuned every 8 samples.
- **Deflection at the edge:**

      η = G · h · (v_filtered + slowBoost · lowpass(v_filtered)) / U  +  turbulence-wobble · b

  - `G = jetGain · exp(0.38 W/h)`, which is spatial Rayleigh growth with a fixed α_i h = 0.38.
  - `slowJetGrowth` scales the exponent by (U_ref/U)^n.
    - When the jet is faster than the reference, the growth is lowered at all frequencies.
    - When it is slower, the extra growth applies only below the band centre, through a one-pole lowpass at f_c.
    - U_ref is the speed whose band centre is the instrument's lowest note.
- **Jet half-width at the edge:** `b = 0.4 h + jetSpreading · turbulent · 0.1 · W`.
  - `turbulent` ramps from 0 to 1 as Re = U h / ν goes from 1000 to 3000.
  - Without spreading, the jet is a fixed-width Bickley-like jet.
- **Flow split (quasi-static, Bickley profile integrated):**

      Q_j = W_w · b · U · flowShare · tanh((η − y_edge) / b)

  `y_edge` is the edge offset: about 0.15–0.2 mm on the whistles, positive into the pipe, plus a tilt control.

### 1.3 Source at the window

- **Jet drive** (Verge / Fabre / Hirschberg jet-drive model):

      Δp_jet = ρ δ_d / A_w · dQ_j/dt,    δ_d = (4/π) √(2 h W)

- **Edge vortex loss** (Verge): `Δp_v = −vortexLoss · ½ ρ (v/0.6) |v/0.6|`. It is symmetric in v, so it produces odd harmonics only.
- **Edge noise:**
  - It is band-limited Gaussian noise, centred at `0.08 · U/h` with a 2-pole lowpass at 1.5 f_c and a highpass at 0.5 f_c.
  - Its level is `breathNoise · 0.15 · ρ U² · 1.14`. The ρU² law replaced U^2.5 on 2026-11-10.
  - It is modulated by `(1 − pulsing + pulsing · sech²((η − y_edge)/b))`: the noise comes in bursts as the jet crosses the edge, with pulsing = 0.8.
  - The same noise also wobbles η by 0.15 · b · breathNoise.
  - An optional "attack chiff" raises the noise after fast pressure rises when Re > 1000.
- **Total source:** `p_src = Δp_jet + Δp_v + edge noise`. It drives the air mass of the window, which is in series with the top of the bore, through the trapezoidal rule.
  - The window's outer end correction (half its equivalent radius) sits in parallel with ρc/A_w, so the window radiates and loses energy roughly as f².
  - That loss changes the second octave's harmonics by less than 1 dB.

### 1.4 Bore and holes

- **Bore:** digital waveguides in sections of at most about 3 cm, up to 64 sections, with Lagrange-interpolated delays. Each section has a biquad fitted to the visco-thermal wall loss and dispersion, scaled by a roughness factor per material.
- **Tone holes:** each hole is a junction with a shunt admittance:
  - inner and outer inertances;
  - radiation resistance ρc/A in parallel with the outer inertance;
  - when covered, the chimney's compliance (scaled by 1 − openness);
  - optional viscous resistance of the gap under a hovering finger (off by default).

  The junction filter is third order in s and is discretised bilinearly per sample, because the openness changes continuously from the sensors.
- **Open foot:** reflection −1 through a one-pole lowpass at ka ≈ 1, with an end correction of 0.61 a.
- **Cross-check:** a passive impulse test of the waveguide bore matches `Acoustics` within about 1–4 cents and about 10 % in Q (`WhistleCheck passive`).

### 1.5 Radiation and output

- **Two monopoles:**
  - the window, with the flow the bore pushes out of it, plus optional direct edge noise;
  - the tube, with the flow out of every open hole and the foot.
- **Listener:** each source is heard as `ρ/(4π r) · dQ/dt` at r, with a path delay per junction for a listener in front of the window.
- **App only:** both sources are then placed at two ears (head model) and passed through a room reverb, −5 dB of headroom and a look-ahead limiter. The limiter and headroom were added 2026-11-10, after the limiter was found to cause a rattle; this is not a model issue. Offline analysis (`WhistleCheck`) takes the model's mono output before any of this.

### 1.6 What sets the harmonics

This matters for the gaps below. Harmonics of the radiated sound are made in four places:

1. **The tanh flow split**, driven by a deflection η that is almost sinusoidal, because the band-pass removes the harmonics of v before they deflect the jet.
   - One stage at Q 0.7 attenuates 4·f_c by about 9 dB, and two stages by about 18 dB. The 1/U scaling and the delay are applied uniformly.
   - Even harmonics come from the offset y_edge/b.
   - Odd harmonics come from how far |η|/b exceeds about 1 (saturation).
2. **The symmetric vortex loss** (odd only, and amplitude-limiting).
3. **The differentiation** dQ_j/dt in the drive (+6 dB/octave), and dQ/dt again at radiation (+6 dB/octave).
4. **The bore's input admittance at the window**, which selects which source harmonics become flow. Harmonics that fall between resonances are suppressed.

---

## 2. The recordings

There are five recordings of rising scales, from the lowest note to the third octave's D, all on a RODE NT1 large-diaphragm condenser. Room and microphone distance were not documented, but were the same session.

| File | Instrument | Preset fitted to it |
|---|---|---|
| `Goldie-High-D-RODE.wav` | Goldie high D (aluminium, narrow head) | "D whistle, Goldie style" (voicing kept from an earlier fit; the user finds it the most playable) |
| `Generation-High-D-RODE.wav` | Generation high D (cheap brass/nickel, plastic fipple) | "D whistle, Generation style" (new; Goldie tube and fipple, own voicing) |
| `Löfgren-High-D-RODE.wav` | Löfgren high D (also filed earlier as "Löfberg"; probably the same whistle) | "D whistle, Löfgren style" (new; Goldie tube and fipple, own voicing) |
| `Goldie-Low-D-RODE.wav` | Goldie low D | "Low D whistle" (voicing left unchanged) |
| `Moeck-Soprano-RODE.wav` | Moeck soprano recorder, baroque fingering | "Soprano recorder in C" (refitted 2026-11-10; alto and tenor share it) |

Older laptop- and phone-microphone recordings (`High-D-Goldie*.wav`, `High-D-Löfberg.wav`, `Low-D-Goldie.wav`) differ from the RODE ones by up to 8–10 dB in noise and low-octave H2. The RODE set is the better reference.

**None of the instruments' geometries were measured.** The presets use forum or typical values:

- Goldie style: a 12 mm bore, an 11.5 mm head over the top 30 mm, a 0.87 mm windway, a 4.2 × 8 mm window, and an edge 0.15 mm into the pipe.
- Generation and Löfgren: the Goldie's tube and fipple, differing only in voicing and edge offset.

So a mismatch can come from wrong dimensions as well as from missing physics.

### How they are measured (`WhistleCheck analyse <wav> <preset>`)

- The recording is split into notes, and only the longest rising run is kept: squeaks and cracked notes are dropped and listed.
- For each note it measures pitch, wobble, level, attack, spectral centroid, H2–H10 in dB relative to the fundamental, and the noise floor between harmonics in four bands.
- It then averages per octave, starting from the lowest note.
- With a preset, the model plays the same scale at each note's "in-tune pressure" (constant breath, found offline), and that sound goes through the same analysis.

The fit score used was the mean absolute difference of the octave-average H2–H5, plus penalties for notes that don't speak.

**Scatter:** in the recordings, harmonic levels vary by ±6–8 dB from note to note (for example Goldie F#5 H2 −9 dB, G5 −22 dB). That comes from room modes, microphone position and the player. Only differences that are systematic across notes and instruments should be read as physics.

---

## 3. Recorded against model: octave averages

These numbers are fresh, measured with the current code (after the ρU² noise law and breath noise 0.04 on the Goldie-fipple presets). They differ by a few dB from the fit numbers quoted in the README, which were measured before the noise change. Values are in dB relative to the fundamental.

### First octave

| Instrument | | H2 | H3 | H4 | H5 | centroid ×f0 | noise 2–4 kHz |
|---|---|---|---|---|---|---|---|
| Goldie high D | recorded | −18.1 | −17.2 | −22.7 | −27.0 | 1.18 | −66.2 |
| | model | −22.2 | −18.6 | **−35.7** | **−39.5** | 1.10 | −68.7 |
| Generation high D | recorded | −13.3 | −21.7 | −30.2 | −31.6 | 1.13 | −75.0 |
| | model | −16.3 | −19.0 | −30.1 | **−43.8** | 1.12 | **−63.8** |
| Löfgren high D | recorded | −10.9 | −13.1 | −28.6 | −30.2 | 1.27 | −67.4 |
| | model | **−17.7** | −15.7 | −28.3 | **−37.9** | 1.18 | −70.0 |
| Goldie low D | recorded | −7.4 | −7.6 | −21.8 | −23.6 | 1.64 | −60.7 |
| | model | **−16.8** | **−22.3** | **−33.3** | **−35.8** | 1.07 | −61.3 |
| Moeck soprano | recorded | −14.9 | −19.1 | −26.7 | −31.8 | 1.18 | −73.0 |
| | model | −20.0 | −20.1 | −27.8 | −29.8 | 1.12 | −75.8 |

### Second octave

| Instrument | | H2 | H3 | H4 | H5 | centroid ×f0 | noise 2–4 kHz |
|---|---|---|---|---|---|---|---|
| Goldie high D | recorded | −29.3 | −24.5 | −35.2 | −30.3 | 1.07 | −65.6 |
| | model | −29.3 | −29.5 | **−53.6** | **−41.5** | 1.01 | −66.0 |
| Generation high D | recorded | −21.6 | −29.2 | −39.6 | −42.2 | 1.02 | −67.9 |
| | model | −21.4 | −30.0 | −43.7 | −42.7 | 1.02 | **−61.0** |
| Löfgren high D | recorded | −32.3 | −26.2 | −40.3 | −38.7 | 1.02 | −64.9 |
| | model | **−23.5** | −25.8 | −42.6 | −36.2 | 1.02 | −68.8 |
| Goldie low D | recorded | −21.4 | −28.4 | −31.6 | −38.6 | 1.05 | −61.4 |
| | model | −26.1 | −29.5 | −38.2 | −36.9 | 1.02 | −63.3 |
| Moeck soprano | recorded | −25.2 | −22.9 | −34.0 | −33.7 | 1.04 | −70.2 |
| | model | −27.0 | −26.0 | −36.8 | **−41.5** | 1.05 | −75.8 |

Bold marks differences of more than about 6 dB, which is beyond the recordings' scatter.

Other differences:

- **Pitch wobble:** the model's is 0.1 cents, against 1–5 cents in the recordings, because the model's scale is played at constant breath. This is not a model gap, since live playing feeds the player's real breath through.
- **Note averaging:** the model plays C natural in the scale; the recordings skip it. So octave 1 averages 8 model notes against 6–7 recorded ones.

### What the tables say

1. **The upper harmonics (H4 and above) are systematically too weak**, on every instrument and in both octaves: typically 8–18 dB short on the Goldie and the low D, less on the recorder.
   - This is the single most consistent gap.
   - The full per-note tables also show H6–H10 in the model falling much faster than in the recordings. For example, Goldie first octave H8–H10 is −32 to −55 dB recorded and −49 to −72 dB in the model.
2. **The drop in H2 between octaves is too small in the model:**

   | Instrument | Recorded | Model |
   |---|---|---|
   | Goldie | 11 dB | 7 dB |
   | Generation | 8 dB | 5 dB |
   | Löfgren | 21 dB | 6 dB |
   | low D | 14 dB | 9 dB |
   | Moeck | 10 dB | 7 dB |

   A voicing that fits the first octave's H2 leaves the second octave too rich, and the reverse.
3. **The low D's first octave is far richer in reality** (H2/H3 −7/−8 dB, centroid 1.64 f0) than the model can play steadily (−17/−22, centroid 1.07). Section 5 explains why "steadily" matters.
4. **Breath noise is roughly right for the Goldie fipple, the Löfgren and the low D** (within about 3 dB). It is too high on the Generation preset (7–11 dB) and a little low on the recorder.
   - The Generation preset shares the Goldie fipple's noise setting (0.04); its recorded noise floor is the quietest of the whistles.
   - Noise "between harmonics" is a weak measure, because it includes sidebands of the tone.
5. **Tuning:** all three recorded high D's play the second octave 11–22 cents sharp of the first (the Goldie most consistently, at +20 to +22), and B5 and C#6 at +12 to +22.
   - The model's presets are tuned by a least-squares tuner that splits the octave stretch between the octaves, so they play in tune by construction.
   - Played gently, the model's second octave is flat, and it needs more breath to reach pitch. The real Goldie played it pure and sharp.
   - This is partly a fitting choice and partly a sign that the model's octave stretch, or its pitch rise with breath, differs from the real instrument's.

---

## 4. Limitations of the current model, with candidate explanations

The candidates are ranked by how well each fits the evidence. None has been confirmed.

### 4.1 Missing upper harmonics

The quasi-static flow split of an almost sinusoidal deflection through a smooth tanh makes few high harmonics, unless the jet swings far beyond its half-width. Making it swing further (jet gain, less vortex loss) adds odd harmonics and makes register changes unstable before it reaches the recorded H4/H5.

Candidates:

- **(a) The jet's frequency response is a band-pass, but real jet theory says it isn't.**
  - `WhistleCheck jet` solved Rayleigh's equation for the real windway profile (a flat core with laminar boundary layers).
  - With receptivity (the v/iω displacement at the flue) included, the response is flat up to ωh/U ≈ 0.15. It then rises up to the jet's cutoff, near Strouhal 0.2: ten times above the model's band.
  - Growth rises about 9 dB per octave, and phase speed rises from 0.33 to 0.59 U.
  - So in reality the harmonics of v do deflect the jet, and the deflection at the edge has harmonics of its own.
  - **But** a broadband jet in this model favours the high bore resonances: notes squeak into upper registers at soft breath, and the octave breaks fall about 30 %. Adachi and Qian (2025) report the same with a measured jet response.
  - In the model, the band-pass does two jobs, register selection and harmonic shaping, and they conflict. This is the key structural issue.
  - Something else must select the register in a real flute. Possibilities:
    - nonlinear saturation of the jet's growth at large amplitude (the fundamental saturates, while small harmonics and other resonances can't start);
    - frequency-dependent phase (dispersion), which puts the high resonances at the wrong phase for growth;
    - amplitude-dependent losses at the edge.
  - A model where the small-signal gain is broadband but the jet's linear growth saturates by amplitude, rather than its flow split only, might keep the register stable and still pass harmonics.
- **(b) Constant convection speed.**
  - The model uses 0.6 U at all frequencies. Theory gives 0.33–0.59 U, rising with frequency.
  - Dispersion changes the relative phase of the harmonics at the edge, and with it the waveform's shape, and it changes which resonances meet the phase condition for growth.
  - A frequency-dependent delay (an all-pass chain, or a delay per band) has not been tried. Phase speed 0.49 U alone, without the theory's band, played normally, with the octave breaks 10–20 % higher.
- **(c) The quasi-static, symmetric profile.**
  - The real jet rolls up into discrete vortices at large amplitude: it is a sinuous jet becoming a vortex street. The split at the edge is then sharper and more impulsive than tanh of a sinusoid, which would raise the high harmonics.
  - The model's jet half-width only grows (spreading), which makes switching smoother. It cannot become sharper with amplitude.
  - Fabre and Hirschberg's discrete-vortex models give a richer drive.
- **(d) The drive's geometry is frequency-independent.**
  - δ_d is a constant. At higher harmonics the effective dipole length and the "where the flow is injected" picture change: the wavelength becomes comparable to the window's size.
  - The jet's own dipole radiation was tried (section 6). Its effect on the source term inside the window has not been tried.
- **(e) Bore, window and radiation at high frequency.**
  - The passive bore matches the `Acoustics` model, but both share the same assumptions: a window as a simple inertance plus radiation, a plane-wave bore, and no cavity under the block or labium.
  - Recordings with a microphone near the window may also pick up near-field and directivity effects, which the two-monopole model ignores.
  - Window radiation and monopole placement each changed the harmonics by only 1–2 dB, so this is probably not the main gap.
- **(f) Wall loss.** If the fitted wall loss and roughness factors are too high at high frequencies, the upper resonances are too low in Q. That damps the source's harmonics, which the bore's admittance must pass. This was checked only against `Acoustics`, not against an impedance measurement.

### 4.2 Too little difference in timbre between the octaves

In the model, H2 comes mainly from the edge offset relative to the jet's half-width: tanh with an offset. The offset is a fixed geometric number, and the only speed-dependent term is the half-width growing with turbulence (`jetSpreading`). That separates the octaves by about 10 dB at most, and it can't produce the Löfgren's 21 dB.

Candidates:

- **(a) Mean jet deflection that depends on speed** (not modelled; ranked high).
  - The jet's mean path bends under transverse forces: a mean pressure difference across the window (pipe interior against outside, from the mean flow and the DC part of the jet drive), and Coanda attraction to the block or labium.
  - A thin jet bends roughly in proportion to Δp / (ρU²) over W²/h. Doubling the speed (second octave) cuts that bending by about 4×.
  - So the *effective* offset would be large in the low register (more H2) and small in the second (purer), which is exactly the trend recorded.
  - It would also make the octave break and the pitch-versus-pressure curve depend on the mean flow.
  - A cheap test: `y_edge_eff = y_edge + k · (ΔP_mean / (½ρU²)) · W` (or a fitted law), with ΔP_mean the low-passed pressure at the top of the bore or the DC jet drive.
- **(b) Asymmetric edge losses.**
  - The vortex loss is symmetric (|v|v) and produces odd harmonics only.
  - Real flow separation differs for outflow and inflow through the window: the labium's inside and outside geometries differ.
  - An asymmetric loss would produce even harmonics that depend on the window velocity's amplitude, which differs between registers.
- **(c) Growth that depends on amplitude.**
  - Real second-octave notes are blown at high Re, where the jet is turbulent and its instability saturates differently.
  - The model's `slowJetGrowth` is a speed-dependent gain, not a saturation, and it applies to all harmonics through the same η.

### 4.3 A rich low D is not stable

The recorded low D has H2/H3 at −7/−8 dB in the first octave. Voicings that come close have been tried: edge offset 0.15–0.25 mm with less vortex loss gave −7/−18. They make 4–6 of 14 quick fingering changes at 300 Pa jump to the second register (`WhistleCheck transition "low D" p=300`), and `glide` finds 6–20× as many odd periods (38–115 chirps). The low D therefore keeps a rounder voicing.

The model gets richness by swinging the jet harder, which also lets the next register win during a transition. A real low D is rich and stable at once, so its richness probably doesn't come from a bigger linear jet swing. This points to the same structural candidates as 4.1(a) and 4.2(a): harmonic content that doesn't depend on the jet's small-signal gain.

The low D geometry (22 mm bore, 1.0 mm windway, 13 × 5.2 mm window) is also from forum values, not measured.

### 4.4 Tuning and pitch against breath

- The presets are tuned to play in tune, while the real whistles stretch the octave by 11–22 cents. The tuner could be told to target the recorded stretch instead, but that would hide whether the model's own octave stretch is right.
- Played gently, the model's second octave is flat, and the pitch rises with breath. The real Goldie's is already sharp when blown softly.
- The lowest note of every preset starts sharp at its threshold (soprano low C: +58 cents).
- Pitch against pressure has not been measured on the real instruments: the recordings are single-dynamic scales. Measuring it needs recordings of crescendos on held notes.

### 4.5 Breath noise

- **Law:** ρU², at a level fitted to the RODE recordings. It replaced U^2.5 because the noise floor relative to the tone is about equal in both octaves on the real whistles. The flutter at 15–80 Hz now matches (7.3 % against a recorded 7.5–7.9 %).
- **Its spectrum** is a fixed-shape band at a fixed Strouhal number (0.08 U/h), which has not been checked against the recordings' noise spectra in detail.
- **Its pulsing** (sech² of the jet's position) is a heuristic.
- **The Generation preset** is 7–11 dB too noisy. It probably needs its own breath-noise value (about 0.015–0.02) rather than a physics change.

### 4.6 Not modelled at all

- The fipple's finer geometry: chamfers, the ramp angle of the block, the labium angle and thickness, and the window's side walls (window width enters only as a scale).
- Hydrodynamic feedback between the edge and the flue (edge-tone behaviour) and the jet-edge interaction beyond the jet-drive dipole.
- Turbulence, beyond the Re-dependent spreading heuristic and the noise source.
- Non-planar modes in the head or window region, and the radiation's directivity.
- The player's vocal tract and mouth cavity, the impedance upstream of the windway, and moisture or condensation in the windway.
- A temperature gradient along the bore (warm breath), and vibration of the walls (except a simple impact resonator for finger taps).

---

## 5. Constraints on any improvement

1. **Playability comes first.** The user plays the model live, from a WARBL controller, and has repeatedly found that offline measures don't predict playability. Of all the presets, the user chose the Goldie style voicing as the most playable, and it must keep playing as it does.
   - Any change should pass, without regressions:
     - `WhistleCheck transition [preset] p=<Pa>`: one hole switched at once. Count register jumps and settling time; currently 0–1 of 14 jumps, settling within 5–20 ms.
     - `WhistleCheck glide [preset] p=<Pa>`: gradual changes. Count odd periods and level dips.
     - `WhistleCheck breath [preset]`: the pressure sweep. No silent gaps, and soft notes must not squeak into upper registers. The octave breaks are about 950 Pa up and 830 Pa down for the D whistle's low D.
     - `WhistleCheck chart [preset]`: every chart note speaks.
2. **Real time:** a 64-sample buffer at 48–96 kHz, with the model at 96 kHz internally. No allocation or locks on the audio thread, and a per-sample cost comparable to today's.
3. **New physics should arrive as a Voicing parameter** whose default keeps existing presets unchanged. Test switches go in `WhistleModel` (like `testEdgeNoise`), and are reachable from `WhistleCheck` arguments.
4. **`PresetTuningTest`** (3 tests) must pass. If a change moves the tuning, rerun `WhistleCheck code` to retune the presets, and paste the new numbers into `WhistleGeometry.cpp`.
5. **Run heavy offline runs at `nice -n 19`:** the user often plays on the same machine.
6. **Before trusting a sound artefact as physics,** compare the app's recording with `WhistleCheck replay` of the same session CSV, on equal terms (same sample rate and model version). The "trrrr" on loud notes, at first blamed on the breath noise, was the app's output limiter.

---

## 6. Tried and left out (2026-11-10 unless dated)

| Experiment | Result | Why it was left out |
|---|---|---|
| Flatten the jet band-pass above 2× its centre | 2nd-octave H4 about +10 dB | Notes squeaked into upper registers at the softest breath (from 107 Pa); octave breaks about 30 % lower |
| The same above 5–8× the centre | +1–3 dB on H4 | Too little; the Generation, Löfgren and low D fits preferred it off |
| The theory's band from Rayleigh, St 0.16, convection 0.49 (2026-11-06) | Most notes stopped speaking (the jet favoured high resonances) | As above |
| The jet's own dipole radiation, ρ δ_d/(4π r c) d²Q_j/dt² | +1–2 dB on H4–H6; H3 moved up to 4 dB depending on listener angle | Small and depends on the listener angle |
| A richer low D (edge 0.15–0.25 mm, less vortex loss) | H2/H3 −7/−18 | 4–6 of 14 transitions jumped register; 6–20× more glide chirps |
| A narrower head (10.5 mm) on the Goldie style | 2nd octave 20–30 cents sharp | Kept 11.5 mm |
| Two band-pass stages on the high whistles | — | Silent gaps between registers; more register jumps (6–7 of 14 at 400 Pa, against 1) |
| Physical finger-gap damping (1.0) | Realistic leakage | Felt sluggish to play; off by default |
| U^2.5 edge noise | 2nd octave 4–5 dB noisier than recorded | Replaced by ρU² |

## 7. Suggested directions, roughly in order of expected value

1. **Separate register selection from harmonic content in the jet** (4.1a). For example:
   - the band-pass (or a phase or dispersion criterion) acts on the jet's *growth* of the fundamental's band, while a broader path carries the harmonics of v to the edge;
   - or an amplitude-saturating growth law, in which the linear gain is broadband but saturates at about b.

   Gate each variant with `transition`, `glide` and `breath` before looking at the spectra.
2. **Speed-dependent mean jet deflection** (4.2a), as a single new term in `y_edge`. It is cheap, and it directly addresses the octave H2 gap.
3. **Frequency-dependent convection (dispersion)** (4.1b), from the phase-speed curve that `JetStability` already computes.
4. **An asymmetric vortex loss** (4.2b).
5. **A discrete-vortex or sharper split at large amplitude** (4.1c), if 1–4 don't close the H4–H10 gap.
6. **Measure, don't guess, the geometry** of at least one real instrument (the Goldie high D): windway height, window length and width, edge offset, and bore and head diameters. This separates "wrong physics" from "wrong dimensions".
7. **Recordings that would help:**
   - crescendos on held notes, for pitch and timbre against breath (ideally with WARBL's pressure sensor in the loop);
   - a close and a distant microphone at once, to separate radiation from room;
   - the same scale at two loudnesses.

## 8. Reproducing the numbers

    cd whistlesim
    W=build/WhistleCheck_artefacts/Release/WhistleCheck
    nice -n 19 $W analyse tools/Real-examples/Goldie-High-D-RODE.wav "Goldie style"
    nice -n 19 $W analyse tools/Real-examples/Generation-High-D-RODE.wav "Generation"
    nice -n 19 $W analyse "tools/Real-examples/Löfgren-High-D-RODE.wav" "fgren"     # name matching chokes on "ö"
    nice -n 19 $W analyse tools/Real-examples/Goldie-Low-D-RODE.wav "low D"
    nice -n 19 $W analyse tools/Real-examples/Moeck-Soprano-RODE.wav "soprano"

`analyse` also takes voicing overrides (`edge=<mm> vortex= st= noise= gain= slow= spread=`), so a candidate voicing can be scored without editing presets. Other useful commands:

- `inside [preset] [fingering]`: the jet's swing over the edge and the harmonics of the jet flow, window flow and sound, at a range of pressures.
- `radiation`: the harmonics from the window and from the holes and foot separately.
- `jet`: the Rayleigh-theory comparison.
- `rough`: inharmonic peaks and flutter on held upper notes.
- `passive`: the bore against `Acoustics`.

---

## 9. Outcome of the review (2026-11-10, same day)

The details and numbers are in `README.md`, "Comparing with real whistles", second pass. In short:

**The main finding corrects sections 4.1, 4.3 and 6.** The model could already make the upper harmonics. Voicings that make them (a jet of constant width, vortex loss around 0.2) were rejected because `transition` and `glide` reported register jumps and chirps, and most of those reports were wrong.

- Both checks timed the raw sound's zero crossings. A note whose third harmonic is within about 6 dB of its fundamental crosses zero several times per period, which read as another register.
- They now time the fundamental only (the sound below 1.5 × the lower note; `raw=1` gives the old reading). The notes that really don't speak at soft breath are still caught.
- With that, a rich voicing on the Goldie fipple at 400 Pa goes from 2 "jumps" and 143 "odd periods" to none of either. The rich low D of section 4.3 is as steady as the current one at 200–500 Pa.

**What changed in the code:**

| Change | Where |
|---|---|
| `transition` and `glide` time the fundamental | `tools/WhistleCheck.cpp` |
| `analyse` prints model minus recording for H2–H8 over the notes both play; the octave split moved from 1.9 × to 1.94 × the lowest note; `blow=recorded`; preset names with ö match | `tools/WhistleCheck.cpp`, `Source/Whistle/ScalePlan.*` |
| Jet bend (direction 2), a new Voicing parameter, 0 by default | `WhistleModel.cpp`, `WhistleGeometry.h`, instrument files, Instrument tab |
| Two new presets, "D whistle, Goldie style (bright)" and "Low D whistle (bright)"; the Generation's breath noise 0.016 | `WhistleGeometry.cpp` |

Every existing preset plays sample for sample as before, except the Generation's noise level.

**Against the recordings** (mean size of the model-minus-recording differences, octave averages of H2–H8):

| Instrument | Before | Now |
|---|---|---|
| Goldie high D | 10.7 dB (Goldie style) | 3.5 dB (bright) |
| Goldie low D | 8.4 dB (Low D whistle) | 3.6 dB (bright) |
| Löfgren, Generation, Moeck | 3.9, 5.1 and 3.7 dB | unchanged: new searches gained 0.3–0.5 dB |

**The directions of section 7:**

1. *Separate register selection from harmonic content:* not needed for H4 and above, given the finding above. A broadband path opened only while the jet swings was tried twice and gave little (the harmonics above the tone holes' cutoff hardly reach the window, so there is little for the jet to amplify).
2. *Speed-dependent mean deflection:* implemented as the jet bend. It separates the octaves' H2 by up to ~14 dB (a fixed edge: ~7). But the recorded Löfgren's H2 goes with the register, not with the breath inside the first octave, so a bend by speed is a partial answer.
3. *Dispersion:* not tried.
4. *Asymmetric vortex loss:* tried at ±50 %: H2 moves 2 dB. Left out.
5. *Sharper split:* not needed; the split's shape (tanh, parabolic, top-hat) changes the harmonics little at a given swing.
6. and 7. *Measurements and recordings:* still the next step. See below.

**Still open:**

- The high D's H3 is 6–8 dB too strong with the bright voicing, and the low D's 10 dB too weak. On the high D the tube's third resonance sits on the third harmonic; the real whistles' wider octaves suggest theirs doesn't. Measured bore and head dimensions would settle it.
- The Löfgren's 20 dB drop in H2 between the octaves.
- Pitch against breath (4.4). To play as sharp as the recorded Goldie the model needs 1.9 kPa on B5 and 3.7 kPa on F#6, so the real scale is itself sharp there.
- The two bright presets, played on 2026-11-10, are good but a little too bright; kept as they are for now. The third harmonic, 6–8 dB too strong on the high D, is the first suspect.
