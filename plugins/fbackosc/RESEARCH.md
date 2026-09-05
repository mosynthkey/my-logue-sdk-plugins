# JP-8080 Feedback OSC — Research Notes

## Source references

| Source | Finding |
| --- | --- |
| [Roland JP-8080 Owner's Manual](https://archive.org/stream/synthmanual-roland-jp-8080-owners-manual/rolandjp-8080ownersmanual_djvu.txt) | DSP oscillator emulating electric-guitar feedback. Control 1 = **HARMONICS**, Control 2 = **FEEDBACK AMOUNT**. Forces mono/legato; no unison. |
| [Sound On Sound JP-8080 review](https://www.soundonsound.com/reviews/roland-jp8080) | Monophonic feedback waveform on OSC 1, distinct from Super Saw. |
| [KVR — JP-8000 oscillator expose](https://www.kvraudio.com/forum/viewtopic.php?t=332263) | Saw into a self-resonating comb filter. State-dependent, can alias. Parameters: Harmonics + Feedback Amount. |
| [Vital forum — Roland feedback oscillator](https://forum.vital.audio/t/roland-style-feedback-oscillator/12153) | Practical recreation: band-limited saw + key-tracked comb filter. Harmonics sweeps comb pitch across ~1 octave. Users add a compressor because the comb level explodes. |
| [Adam Szabo on JP-8000 Feedback OSC](https://audiosex.pro/threads/adam-szabo-jp6k.30560/) | Consensus model: saw (or Super Saw) into a variable-feedback comb, then distortion. ROM coefficients were never published. |

## Algorithm (this implementation)

The original JP-8080 ROM has not been reverse-engineered here. This unit follows the consensus model:

1. **Exciter** — band-limited saw (`osc_bl2_sawf` on logue hosts, polyBLEP fallback otherwise), keyed to note pitch.
2. **Comb resonator** — `y[n] = x[n] + fb * HPF(y[n - D]) * damping` with key-tracked delay  
   `D = 1 / (w0 × harmonic_ratio)` (`w0` is cycles/sample).
3. **HARMONICS** — maps 0…1 → `harmonic_ratio = 2^((h - 0.5) × 2)` (≈0.25…4, centred at 1).
4. **FEED** — maps 0…1 → feedback coefficient up to 0.93.
5. **Output** — feedback-gain compensation + cubic soft clip + DC blocker.

## Level (why the first version was too loud)

A naive IIR comb `y = x + fb * y_delayed` has peak gain `1 / (1 - fb)`.

| FEED | fb | Peak gain | Uncompensated vs saw |
| --- | --- | --- | --- |
| 0 | 0 | 1× | 0 dB |
| 0.45 | 0.42 | 1.7× | +4.7 dB |
| 1.0 | 0.93 | 14.3× | +23 dB |

The first cut clipped the comb to ±4, multiplied by 0.55, then applied a broken soft-clip (`x / |x|` once `|x| ≥ 1`). At high FEED the comb sat on the rails, so the oscillator became a full-scale square. On top of that the “DC blocker” was `d = x + 0.995 d` (a leaky integrator with gain ≈200) subtracted from `x`, so any leftover DC became a huge offset. That is much louder than a JP-8080 patch sitting next to a plain saw.

The original hardware almost certainly did **not** leave `1/(1-fb)` uncompensated:

- Fixed-point DSP on the JE-8086 would have saturated long before +23 dB.
- Recreations (Vital thread, Szabo) describe **comb then distortion**, i.e. a saturator that also acts as a compressor.
- The AMP section is a separate stage; the OSC waveform itself is meant to sit with saw / square / Super Saw.

This unit now:

1. DC-blocks **inside** the feedback loop with a true one-pole high-pass so the comb cannot integrate a DC bomb (gain `1/(1-fb)` at 0 Hz).
2. Applies a leaky damping (`0.97`) on the recirculating sample — guitar-amp bandwidth, also helps stability.
3. Compensates most of the resonant gain:  
   `out = y * trim * (1 - fb) / (1 - 0.28 * fb)`  
   so peak stays near a single saw (`trim = 0.42`) and only rises a few dB at max FEED.
4. Uses a cubic soft clip (`x - x³/3` in ±1) instead of a hard sign function.
5. Output DC blocker is the textbook `y = x - x[n-1] + R y[n-1]`, not a leaky integrator.

The leftover rise is intentional: the JP-8080 Feedback OSC does get more intense as FEEDBACK AMOUNT goes up. It should not jump to 0 dBFS.

## JP-8080 behaviour not replicated

- Forced monophony (host synth handles voice allocation).
- Higher internal sample rate used by Super Saw on the original hardware.
- Exact harmonic-ratio curve and feedback scaling from ROM.

## Parameters

| Param | JP-8080 | Role |
| --- | --- | --- |
| HARM | Control 1 / HARMONICS | Comb harmonic spacing |
| FEED | Control 2 / FEEDBACK AMOUNT | Resonance / feedback level |
