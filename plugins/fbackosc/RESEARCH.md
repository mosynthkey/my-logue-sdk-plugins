# JP-8080 Feedback OSC — Research Notes

## Source references

| Source | Finding |
| --- | --- |
| [Roland JP-8080 Owner's Manual](https://archive.org/stream/synthmanual-roland-jp-8080-owners-manual/rolandjp-8080ownersmanual_djvu.txt) | DSP oscillator emulating electric-guitar feedback. Control 1 = **HARMONICS**, Control 2 = **FEEDBACK AMOUNT**. Forces mono/legato; no unison. |
| [Sound On Sound JP-8080 review](https://www.soundonsound.com/reviews/roland-jp8080) | Monophonic feedback waveform on OSC 1, distinct from Super Saw. |
| [KVR — JP-8000 oscillator expose](https://www.kvraudio.com/forum/viewtopic.php?t=332263) | Saw into a self-resonating comb filter. State-dependent, can alias. Parameters: Harmonics + Feedback Amount. |
| [Vital forum — Roland feedback oscillator](https://forum.vital.audio/t/roland-style-feedback-oscillator/12153) | Practical recreation: band-limited saw + key-tracked comb filter. Harmonics sweeps comb pitch across ~1 octave. |

## Algorithm (this implementation)

The original JP-8080 ROM has not been reverse-engineered here. This unit follows the consensus model:

1. **Exciter** — band-limited saw (`osc_bl2_sawf`), keyed to note pitch.
2. **Comb resonator** — `y[n] = x[n] + fb * y[n - D]` with key-tracked delay  
   `D = 2π / (ω₀ × harmonic_ratio)`.
3. **HARMONICS** — maps 0…1 → `harmonic_ratio = 2^((h - 0.5) × 2)` (≈0.25…4, centred at 1).
4. **FEED** — maps 0…1 → feedback coefficient up to 0.93.
5. **Output** — soft saturation + DC blocker to tame comb DC buildup.

## JP-8080 behaviour not replicated

- Forced monophony (host synth handles voice allocation).
- Higher internal sample rate used by Super Saw on the original hardware.
- Exact harmonic-ratio curve and feedback scaling from ROM.

## Parameters

| Param | JP-8080 | Role |
| --- | --- | --- |
| HARM | Control 1 / HARMONICS | Comb harmonic spacing |
| FEED | Control 2 / FEEDBACK AMOUNT | Resonance / feedback level |
