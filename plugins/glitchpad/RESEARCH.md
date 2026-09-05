# GlitchPad research

Illformed Glitch² is a sequencer-driven multi-effect: 128 MIDI-triggered
scenes, each with a pattern of modules (Retrigger, Reverser, Shuffler,
Tape Stop, Stretcher, Gater, Lofi, Delay, Distortion, Modulator) plus a
Randomizer that only picks weights.

NTS-3 cannot host that editor. A genericfx unit has an XY pad, Depth,
and at most eight parameters. `unit_render` input is muted while the pad
is up; firmware 1.4+ `get_raw_input` still carries AUDIO IN, which is
how TransitionLooper pre-rolls a loop.

## Mapping

Glitch² scenes are MIDI notes. The pad plays the same idea:

| Glitch² | GlitchPad |
| --- | --- |
| Scene / MIDI note | X = MODE (8 scenes) |
| Timing / retrigger speed | Y = TIME (1/2 … 1/64) |
| Trigger Gate / Latch | HOLD |
| Even / triplet / dotted / free | SYNC |
| Master mix | Depth = MIX |
| Stacked Lofi | CRUSH overlay |
| Retrigger decay, tape length, gater smoothing, delay feedback | DECAY |

Modules that need a captured buffer (RTRG, REV, SHUF, TAPE, STRCH) freeze
the most recent slice when the pad goes down. GATE, CRUSH, and DLY run
on live input. Silent pre-roll arms a live capture, same as
TransitionLooper.

What is intentionally not ported: 128-scene banks, the multi-lane
pattern editor, per-module filters/mixers, Modulator, Distortion as
their own scenes, and the Randomizer module. Those need a host UI NTS-3
does not have.

## Memory

NTS-3 genericfx SDRAM is 3 MB per runtime. The stereo capture buffer is
one bar at 40 BPM (288000 frames) plus a 1 s delay line: 672000 floats,
about 2.56 MB. Same budget as TransitionLooper.

## Sources

- [Illformed Glitch²](https://illformed.com/glitch/)
- [Glitch 2.1.3 User Guide](https://illformed.com/downloads/glitch_2_1_4/Glitch2_User_Guide.pdf)
