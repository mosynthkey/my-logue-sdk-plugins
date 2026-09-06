# HSnare Research and Implementation Notes

Sibling of HClap: a separate NTS-3 unit for the TR-808 / TR-909 *snare*,
not a sample and not a clap with different filters. Both snares are analog.

## Hardware paths

### TR-808 (bridged-T + one snappy)

Service notes + Kurt Werner / Norgatronics (rev B, the common later board):

```
trigger pulse (~1 ms)
    ├─ ping bridged-T "fundamental"   ~173 Hz
    ├─ ping bridged-T "harmonic"      ~336 Hz   (ratio ~1.94, not exact 2:1)
    └─ noise VCA envelope ─ high-pass ─ "Snappy"
Tone (VR8) mixes the two shells. Snappy (VR9) is noise amount
(and also scales the pulse into the T-networks). Level only besides that.
```

Early boards (rev A) sat near 250 / 499 Hz. Roland then dropped both
caps (C58 / C61). Isolated later 808s measure around 173 / 336 Hz.
This unit uses rev B.

Bridged-T output is a decaying sine. Decay is set by Q, not a separate
Decay knob. The printed "60 ms" table looks like the modular spec, not
the shipping RC.

Noise is the shared 2SC828-R transistor source (same as clap / toms).

### TR-909 (two triangle VCOs + split snappy)

Network-909 and the TR-909 Service Notes:

```
TRIG
    ├─ reset VCO-1 / VCO-2 in phase (triangle, then diode clip ≈ sine)
    ├─ ENV1 → CV gen: ~20 ms pitch bend
    ├─ ENV2 / ENV3 → VCA on each VCO (accent-scaled)
    └─ LFSR noise
           ├─ LPF → VCA ENV4          (body of snappy)
           └─ HPF → VCA ENV5/accent   (crack)
Tune moves both VCOs. Tone tilts the mix / noise colour. Snappy is
mostly the high-passed crack. Drum + Snappy mix to Level.
```

The 909 shell is *not* a bridged-T. It is a hysteresis-comparator
triangle VCO with a voltage-dependent integrator, reset together so
the two partials start in phase. That plus the 20 ms droop is why a
909 snare "thwacks" then sits, instead of just ringing two sines.

Noise is the same 31-stage LFSR as the clap (~300 kHz, Electric Druid).

### Front panels

| | 808 | 909 |
| --- | --- | --- |
| Tune | — (fixed T-network) | yes |
| Tone | shell mix | shell / noise colour |
| Snappy | noise (+ pulse amount) | HPF crack (ENV5) |
| Level | yes | yes |
| Decay | no | no |

## What this unit models

Circuit-informed, no libm, no samples:

```
analog LCG noise  ─┐
31-bit LFSR       ─┴─ Y crossfade

Y = 0 (808): decaying sines at 173 / 336 Hz + one HPF snappy
Y = 1 (909): clipped triangles, 20 ms pitch bend, LPF+HPF snappy
```

TUNE transposes both shells (the 909 pot; on 808 it is the "what if").
TONE is the shell mix. SNAP is snappy vs body.

Not modelled: BA662 feedthrough, every diode in the clipper, analog
clock jitter, the 24 ms flat-top on some 909 snappy envelopes.

## NTS-3 mapping

Same pad idea as HClap, different voice:

- **X / DENS** — 16-step Euclidean phrase, rotated onto 2 and 4
- **Y / TYPE** — 808 (bottom) → 909 (top)
- **TONE / SNAP / TUNE** — the knobs the machines actually had
  (Tune is 909-only on hardware)

## Sources

- Roland TR-808 Service Notes, SD (bridged-T, Tone VR8, Snappy VR9)
- Roland TR-909 Service Notes / [network-909 Snare Drum](http://www.network-909.de/snaredru.htm)
- [Norgatronics, 808 Snare Mutations](https://norgatronics.blogspot.com/2021/11/808-snare-mutations.html) (rev A vs B Hz)
- Kurt Werner, ChucK 808 snare (bridged-T frequencies)
- [Electric Druid, TR-909 noise](https://electricdruid.net/tr-909-noise-generator/)
- [firstpr TR-909 sound mods](https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf)
- Tiptop SD808 / Waldorf Attack notes (909: two osc + split noise)

Not affiliated with Roland.
