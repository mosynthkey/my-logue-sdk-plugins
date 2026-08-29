# HyperSaw Research and Implementation Notes

This document summarizes what is known about the Access Virus TI HyperSaw oscillator from public sources, what was inferred for this logue SDK unit, and how the implementation approximates the original behavior.

## What the Virus Manual Documents

From the [Virus TI Parameter Reference Manual](https://cf3.zzounds.com/media/Virus_TI_Reference-71288db0dd1d87b69b5e3e91d9c87719.pdf):

- **HyperSaw** layers up to **9 detuned sawtooth waves** per oscillator voice.
- **Density (1.0–9.0)** selects how many saws are active. Levels are **cross-faded** for smooth transitions (mod destination: Osc Shape).
- **Spread (0–127)** controls detuning between the individual saws (mod destination: Osc Pulse Width).
- Each saw has an integrated **HyperSub**: a **square wave one octave below** that saw. The classic Sub Oscillator volume control **cross-fades** between HyperSaw and HyperSub when HyperSaw mode is active.
- HyperSaw follows a **logarithmic detune scale** similar to the Roland JP-8000 Super Saw, but with two additional detuned voices (9 vs 7).
- Internal sync behavior exists on the Virus but is outside the scope of this single oscillator unit.

## Community Reverse-Engineering (Not Virus Firmware)

No public dump of Virus HyperSaw DSP code was used. The following sources informed the approximation:

| Source | Relevant findings |
| --- | --- |
| [Adam Szabo – How to Emulate the Super Saw](https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf) | JP-8000 detune is **non-linear** in the MIDI control; offset ratios are **pitch-proportional**; center oscillator stays at unity ratio. |
| [A to Synth – Super Saw code analysis](http://atosynth.blogspot.com/2026/02/the-super-saw-code.html) | JP-8080 DSP uses coefficient table `{0, 128, -128, 408, -412, 704, -720}` scaled by pitch and detune amount with multiply-high arithmetic. |
| [Kulshan Studios – Virus TI HyperSaw](https://kulshanstudios.com/blog/tag/virus+ti+hypersaw) | Virus HyperSaw extends JP SuperSaw with **2 extra saws** and follows the same **logarithmic detune** character. |
| [Virus user forum (infekted.org)](http://www.infekted.org/virus/showthread.php?t=31424) | Virus detune curves are **logarithmic**: most resolution in the lower half of the control range. |

## Implementation Mapping

### Band-limited oscillators

Uses logue SDK `osc_bl2_sawf()` and `osc_bl2_sqrf()` with `osc_bl_saw_idx(note)` for note-dependent anti-aliasing, matching the official `waves` oscillator template approach.

### Detune distribution

- Base coefficient layout follows the JP-8080 table, extended to 9 voices by adding an outer pair `±960` (inferred geometric continuation of `{±704, ±720}`).
- Per-voice frequency ratio: `1 + (coeff × spread_curve) / 720`.
- **Spread curve**: 17-point LUT from Szabo's JP-8000 MIDI sampling (0, 7, 15, … 127), linearly interpolated. This reproduces the fine control near zero and steep rise at high spread.

### Density (gain behavior)

- Parameter maps 0–1023 → density 1.0–9.0.
- Voice `i` gain = `saturate(density - i)` for sequential activation from center outward.
- **Center voice is never attenuated** when additional voices fade in (Virus-style vs JP Mix).
- Output normalized by `1 / sqrt(Σ gain²)` with a fixed trim to limit clipping.

### Phase behavior

- On each note-on, all 9 saw and sub phases are randomized via `osc_white()` for the drifting warmth typical of multi-oscillator stacks.

### Stereo spread

- Alternating pan positions per detuned pair; **WIDTH** blends between mono (0.5/0.5) and wide image.
- NTS-1 mkII output is mono: left/right are averaged.
- microKORG2 writes the mono sum per synth voice into the interleaved output buffer.

### HyperSub

- Each active saw voice drives a band-limited square at **half frequency** (one octave down).
- **SUB** cross-fades between the saw stack and the sub stack, matching the Virus HyperSub description.

## Known Limitations

- Detune coefficients are **JP-8080-derived**, not verified against Virus ROM.
- Spread curve is from **Super Saw** analysis; Virus Spread may differ slightly.
- No HyperSync, FM Amount sync offset, or filter-key-tracking emulation.
- microKORG2 build uses the KORG **drumlogue/microKORG2 Docker toolchain** (`arm-unknown-linux-gnueabihf-gcc`). The repo Makefile auto-detects it under `third_party/logue-sdk/tools/microkorg2-toolchain/`; see the SDK `docker/docker-app/builder/get_microkorg2_toolchain.sh` download script.

## Parameters

| UI | Range | Default | Behavior |
| --- | --- | --- | --- |
| DENS | 0–1023 | 512 (~5.0) | Active saw count 1–9 with crossfade |
| SPRD | 0–1023 | 460 | Detune spread (non-linear curve) |
| SUB | 0–1023 | 0 | HyperSub square mix |
| WIDTH | 0–1023 | 768 | Stereo spread of detuned voices |
