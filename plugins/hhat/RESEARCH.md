# HHat Research and Implementation Notes

Sibling of HSnare / HClap: an NTS-3 phrase pad for *modeled* hi-hat,
not a WAV one-shot. Classical drum machines expose Closed and Open as
two keys. Real playing lives mostly *between* those extremes (half-open,
loose closed, foot choke). This unit puts that continuum on Y.

## What classic machines actually do

### TR-808 / TR-606 (analog metal)

Shared bank of **six Schmitt-trigger square oscillators** (Werner /
Baratatronix / service notes), mixed into an inharmonic “metal hum”,
then band-pass filtered. Hats take the **high** path (~7100 Hz BPF on
the 808), VCA + resonant / Sallen-Key HPF.

| Voice | Decay (808 service chart) |
| --- | --- |
| Closed | fixed ~50 ms |
| Open | 90–600 ms pot |
| Cymbal | 350–1200 ms (extra low BPF band) |

Open and closed share the *source*. The musical difference is almost
entirely **envelope length**, plus the exclusive choke group: a closed
trigger shortens / kills a ringing open. On the 606 the open decay also
interacts with closed triggers mid-note (variable choke timing).

Ideal oscillator frequencies from Werner’s SPICE (factory trim on the
last two):

```
205.3, 304.4, 369.6, 522.7, 540, 800 Hz
```

Avoid exact harmonic multiples. Exact cents matter less than the
inharmonic swarm.

### TR-909 (hybrid)

Closed / open hats are **6-bit PCM** with analog amplitude shaping.
Different ROM windows, shared choke group, Decay knobs. Not what this
unit models — Ride909 already covers the ROM path for ride.

### Acoustic / e-drum continuous control

Sample libraries and e-drums use many articulations (tight → loose →
half → open → foot splash) selected by CC4 / pedal, plus choke fades
when the pedal closes while a note rings. Continuous synthesis can skip
sample switching: **one source, continuous τ**, with live choke when Y
drops or a more-closed hit arrives.

## Open-source / published models reviewed

| Source | What it is | Use here |
| --- | --- | --- |
| Werner et al., ICMC 2014 (808 cymbal) | Circuit-informed digital model of the six squares + BPF/HPF/VCA | Osc freqs, topology |
| Baratatronix 808 / 606 / KR-55 write-ups | Practical patch language restatement | Decay numbers, choke story |
| Erica / Moritz Klein EDU + 606 video | Six squares → passive BPF → CH/OH envelopes | Confirm metal-noise approach |
| Erica Black Hi-Hats vibe patch | 7 pulses + HPF noise, separate CH/OH, exclusive latch | Tick + noise shimmer extras |
| Trap808 / UKGarage in this repo | Discrete CH/OH + choke, age-based `fasterexpf(-age/τ)` | Envelope / choke pattern |
| PercIter | Y Skin↔Metal morph | Continuous Y morph idea |
| HSnare / HClap | Euclidean X on hold-to-run pad | Phrase / clock scaffold |

Trap808 hats today are bright noise × two discrete τ values (~28 ms /
~180 ms). Fine for a kit layer; not a metal model and not continuous.

## Design chosen for HHat

```
free-running 6× square (808 freqs, TUNE transpose)
        → mix
        → SVF band-pass (TONE moves centre ~5–9 kHz)
        → per-voice amp  (age-based exp; τ from Y + DEC)
        → one-pole HPF   (closed brighter / tighter)
        + short stick tick (HPF noise × fast env)
```

- **X = DENS**: `fx::euclidHit` on 16 steps (no 2-and-4 rotation —
  hats sit on the grid from step 0). High dens adds 32nd rolls.
- **Y = OPEN**: Close → Open. Maps to τ (~45–320 ms × DEC), HPF cut,
  and a little noise shimmer. Openness is sampled at trigger; dropping
  Y while a voice rings applies a foot choke (accelerates age).
- **Choke group**: a new hit with lower openness clears / shortens
  more-open voices (808 exclusive behaviour).
- **Envelopes**: age-based `fasterexpf(-age/τ)` only — never
  `fasterexpf` for near-1 per-sample coeffs (HSnare pitfall).
- **No libm**: squares from phase compare, `float_math.h` only,
  `ULIBS` empty.

## Param map

| Role | Name | Meaning |
| --- | --- | --- |
| X | DENS | Euclidean hits / bar (1–16), then 32nd rolls |
| Y | OPEN | Close → Open continuum |
| Depth | MIX | Dry/wet |
| Edit | TONE | BPF centre / brightness |
| Edit | TUNE | Metal oscillator transpose |
| Edit | DEC | Master decay scale |

## Sources

- Kurt Werner et al., *The TR-808 Cymbal: a Physically-Informed,
  Circuit-Bendable, Digital Model* (ICMC 2014)
- Roland TR-808 / TR-606 service notes (cymbal / hi-hat block)
- [Baratatronix — 808 cymbal & hi-hat](https://www.baratatronix.com/blog/cascadia-808-cymbal-hi-hat-synthesis)
- [Baratatronix — 606 cymbal & hi-hat](https://www.baratatronix.com/blog/606-cymbal-and-hi-hat-synthesis)
- Moritz Klein, *Designing a TR-606 style hi-hat from scratch*
- Raygum, *The Roland TR-909 Monograph* (PCM hats — out of scope here)

Not affiliated with Roland.
