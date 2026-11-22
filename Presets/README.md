# Built-in presets

Each file here is one built-in instrument in the format **Export…** writes: JSON with the bore, holes, fipple, tuning
goal and voicing, lengths in mm. They are built into the app, the tests and WhistleCheck, and listed in file-name
order. Each whistle shows twice in the app, on WARBL's holes L1–R3 and L2–R4: the same instrument, played with the
upper or the lower six fingers. That copy is made when the presets load, so it has no file of its own.

To change a preset, edit its file and rebuild. `WhistleCheck code [preset]` retunes the designed presets to the
pitch the model plays, judged where each note is blown (Tuning workshop, PlayedTuning), and writes the new bore length,
head section and holes back into these files. The measured Golden presets keep the real whistle's dimensions and
aren't retuned. PlayedTuningTest checks that the designed presets play in tune.

Until 2026-11-22 the presets were written in C++ (`Source/Whistle/WhistleGeometry.cpp`, see its git history). They
were moved here unchanged: every preset and voicing came out identical (`WhistleCheck exportpresets <dir> all`,
before and after).

Where each comes from, and how its voicing was found:

## High D whistles

- **Cylindrical metal, conical metal, wide head**: typical dimensions, not measured. The conical body is 13 mm at
  the top and 10.5 mm at the foot. The wide head started as a narrower bore around the fipple, 10.5 mm widening to
  the 12 mm bore over 30 mm. Tuned to the played pitch its head came out a little wider than the bore, hence the name
  (it was "narrow head" until 2026-11-22). Their voicings were searched against the recorded whistles (first octave
  H2/H3 about −17 dB, second octave −32/−28) and for steady playing. One band-pass stage is what steadies quick
  fingering changes: register jumps at a soft breath went from 7 of 14 to 1, and settling from ~200 ms to ~6 ms.
  Jet spreading makes the second octave purer and louder, less vortex loss gives the first octave back its odd
  harmonics, and slow-jet growth separates the two octaves.
- **Wood**: the Golden style's fipple (the easiest to play) on a thick-walled wooden tube.
- **Golden style**: in the style of a Golden high D, not measured: a thin aluminium tube, a low windway ceiling
  (Golden's "medium" is about 0.87 mm) and a short window (4.2 × 8 mm), the edge 0.15 mm into the pipe (the
  recorded Golden's second harmonic). Voicing fitted to `tools/Real-examples/High-D-Golden.wav` (WhistleCheck
  analyse): the thin windway wants a lower jet band; the jet settings make a softly blown low note swing far across
  the edge (rich, like the recording's bottom D) while the hard-blown second octave stays nearly pure. The jet widens
  as it turns turbulent, the second octave's more, so that octave is louder and purer (H2 −30 dB, was −17). Breath
  noise as the RODE recordings of the Golden and Löf: −66 to −69 dB between the harmonics.
- **Golden style (bright)**: the same whistle voiced for the upper harmonics of `Golden-High-D-RODE.wav`. A jet of
  constant width (no spreading) and little vortex loss switches harder at the edge, which is where the harmonics from
  the fourth up come from (H4–H8 within ~5 dB of the recording in both octaves; the Golden style is 9–23 dB short).
  The jet bend, with the edge nearly centred, makes the second octave purer instead of the spreading. H3 is still too
  strong by ~7 dB. Played on 2026-11-10: good, if a little too bright.
- **Generation style, Löf style**: the Golden fipple, voiced by a search against RODE recordings of those whistles
  (octave averages of H2–H5, note by note), then checked with transition, glide and breath. Generation: strong even,
  weak odd harmonics in the first octave (H2/H3 −13/−22 dB), and the quietest between its harmonics (−75 and −68 dB).
  Löf: a rich first octave (H2/H3 −11/−13 dB) and a pure second (−31/−27), its edge 0.2 mm in (H2 −11 dB).
  Their tubes and windways are not measured.

## The measured Golden

- **Golden (measured)**: a Golden high D as measured (`docs/Golden.md`): a 12.5 mm bore in a 1.8 mm wall, a
  0.97 × 7.7 mm windway 24 mm long and a 5.8 mm window. The head is the tube pressed flat at the sides to 9 mm wide
  (101.8 mm², as a round bore of 11.4 mm); it runs 14 mm from the flue exit (8.2 mm past the edge), then widens to
  the bore over 4 mm. The model has one width for the jet and the window: the windway's 7.7 mm. The bore (265 mm,
  edge to foot) and the holes are the real whistle's, not the tuner's. Voicing searched against
  `Golden-High-D-RODE-measured.wav`: how bright a note is above H6 follows how far the jet swings across the edge;
  vortex loss 0.24 swings it ~3 half-widths in the first octave and ~2 in the second.
- **… real pressure**: the same whistle voiced for the mouth pressures measured on it (`Golden-pressure-1-swells-*`,
  WARBL's mouthpiece in the mouth beside the whistle's): D5 ~150–310 Pa, G5 ~190–700, B5 ~300–880, D6 ~420–1000, where
  the voicing above needs 1.3–3 × that. Searched pressure by pressure (level, pitch, H2, H3, H5, H7–H10): the jet's band
  higher, a little less jet gain and bend, the edge on the jet's centre line. WARBL's breath goes straight through:
  the breath response's ~2 × is undone, so the model gets the mouth pressure WARBL measures.
- **… real pressure and breaks**: also matches where the registers change on the measured breath
  (`Golden-pressure-3-breaks-*`, and the tune `Golden-pressure-4-tune-Whalley-Range-*`): the jet flattened above its
  band gives the upper registers their gain, less of it at low Reynolds numbers keeps the low notes from starting at
  a sigh. Breaks up at 0.99 × the real pressure, the third register at 1.01 ×, falling back at 1.26 × (was 1.44 ×).

## Low D whistles

- **Low D whistle**: a 22 mm cylindrical bore (a Golden low D measures ~21.8), a 1.0 mm windway and a 13 mm wide
  window, from forum measurements of low whistles. The window is short (5.2 mm, Overton style): a longer one let the
  jet grow so much that quick fingering changes flipped into the octave. The fipple is bigger (28 mm windway). A slightly
  wider head over the top 60 mm flattens the second octave into tune. Voicing: a jet band low enough for its registers
  to sit where a low D is blown (low D ~120–670 Pa, the octave from ~670 Pa), one band-pass stage, and less vortex loss
  for the reedy odd harmonics of a real low D (`Low-D-Golden.wav`).
- **Low D whistle (bright)**: voiced after `Golden-Low-D-RODE.wav`, whose low register is far richer: the edge 0.25 mm
  into the pipe, less vortex loss and the jet's full gain give the first octave's H2 −7 dB (recorded −7), H4, H6 and H8
  within 1 dB. Its H3 stays ~10 dB short. Played on 2026-11-10: good, if a little too bright.

## Härjedalspipa

- **Härjedalspipa (A#, traditional scale)**: a Swedish wooden duct flute with a cylindrical bore and six finger holes,
  fingered as a whistle a major third down (the whistle chart, transposed −4) and tuned to its traditional scale as
  Swedish Wikipedia gives it (the tuning goal in the file). The high tonic is overblown with every hole covered; the
  cross-fingered seventh is left out. Typical dimensions, scaled from the wooden whistle, not a measured instrument;
  the tuner placed the holes. R1 small and R2 large (4 and 10 mm) keep D+35 and D#, only 65 cents apart, about
  20 mm apart.

## Recorders

- **Soprano recorder in C (baroque fingering)**: a reverse-conical bore (13 mm at the top, 9.5 at the foot), the thumb
  hole on the back, all seven front holes, thick walls and a wide windway. Typical dimensions, not measured; the
  window is 3.7 × 8.8 mm as a measured soprano (NMMSD). A short window for the jet's thickness grows the jet less on
  its way to the edge: a purer tone, and the top notes speak at a lower breath. The edge 0.2 mm into the pipe gives
  the recorded Moeck soprano's strong second harmonic. The bell sensor closes the foot (top C#). Voicing fitted to
  `Moeck-Soprano-RODE.wav` (first octave H2/H3/H4 −15/−19/−27 dB), with a breath curve so that low C speaks at 80 Pa
  of WARBL breath and top C needs 1.8 kPa.
- **Alto in F, tenor in C**: the soprano scaled geometrically (mouth too, so the same W/h and f·h/U keep the jet's
  behaviour), then retuned: losses and the window's end correction don't scale exactly.
- **Soprano … real pressure**: voiced for the mouth pressures measured on a Moeck soprano
  (`Moeck-pressure-2-swells-*`): its scale spans only ~2.5 × in pressure (low C ~60–190 Pa, the pinched E6–A6
  ~200–430). The jet's band twice as high (Strouhal 0.04), less vortex loss and slow-jet growth, the edge 0.3 mm in;
  WARBL's breath goes straight through. The pinched notes still need ~400 Pa (the Moeck's from ~210): their trouble is
  in the bore and holes, which aren't the Moeck's.

## Loudness and finger impact

Every preset plays at about the loudness of the measured Golden, −14.6 dBFS over its test scale (WhistleCheck
scale), through its output gain. A finger landing pushes air in (finger tap) and knocks the body, whose resonance is
set by material (first guesses until measured: wood low and short, thin metal higher and ringing).
