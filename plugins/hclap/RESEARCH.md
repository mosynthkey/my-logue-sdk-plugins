# HClap Research and Implementation Notes

This unit starts from the original TR-808 / TR-909 hand-clap circuits, not from a sample.
Both machines only expose a Level knob. The interesting sound is the topology.

## There is no ROM to dump

Ride909 could chase a hashable PCM chip. Clap cannot. Roland never sampled it:

- TR-808: fully analog. Selected 2SC828-R transistor noise, shared with snare / toms / maracas.
- TR-909: analog clap (and kick / snare / toms / rim). Digital noise is an LFSR. Hats / ride / crash are the 6-bit ROMs.

A "faithful original" is a circuit model. Sample packs and most software 808s are one-shots.

## Hardware path (both machines)

Network-909, the TR-808 Service Notes (CP / MA, pp. 6–9, 14), and Colin Fraser all describe the same skeleton:

```
shared noise
    → band-pass (the "clap colour")
    → two amplitude paths
         1. burst VCA  — quad-comparator sawtooth train ("several hands")
         2. tail VCA   — slower decay ("fake reverb", not a reverberator)
    → mix / level
```

### Burst envelope (the clap)

A trigger starts a ~30 ms pulse. A quad comparator then fires four attack–decay
ramps in series: finishing the first starts the second. Each of the first three
is a falling sawtooth about 10 ms long. The fourth discharge is longer (~20 ms).
Total burst span is about 50 ms.

This is not four square gates. The service-manual scope trace is a sawtooth
train. That falling ramp is why a real 808/909 clap "chatters" instead of
going `ch-ch-ch-ch`.

Service-notes typo: C140 is 27 nF, not 2n7. With R350 = 1 M the burst RC is
~27 ms. Colin Fraser and later builders only got the three rapid ramps after
correcting that cap.

### Tail envelope (not reverb)

C139 = 18 nF, R348 = 1 M on the 808. The service notes and later write-ups
call the audible result ~100 ms. It is a decaying noise bloom that stands in
for a room. Synthchaser's "reverb envelope" mod is this RC, not an actual
reverb tank.

### 808 vs 909 (the differences that matter)

| | TR-808 | TR-909 |
| --- | --- | --- |
| Noise | Analog transistor (2SC828-R, cal. 130 mV RMS at TP4) | 31-stage LFSR, ~300 kHz clock (Electric Druid) |
| VCA topology | One VCA. Burst CV and tail CV are summed first (Colin Fraser, synth-diy 2006) | Two VCAs. Crack and tail are separate (909, Amdek HC-2) |
| Filtering | One band-pass into that VCA. Mods treat R332 = 27 k as the colour pot | Same idea, but crack and tail are filtered differently |
| Accent | Trigger / accent bus | Extra D/A velocity into the VCA |
| Front panel | Level only | Level only |

Baratatronix / service-note working numbers used here:

- Band-pass centre ≈ 1 kHz (B5 +21 ¢) on the 808
- Burst: 10 ms × 3 + ~20 ms final saw
- Tail: ~100 ms, mixed under the burst

909 clap is usually heard slightly brighter and a bit more "split" (crack vs
room) because of the dual VCA and the digital noise grain.

## Open-source models reviewed

None of these is a drop-in gold standard the way the 909 Ride ROM is.

| Source | What it actually is | Use here |
| --- | --- | --- |
| TR-808 Service Notes + [network-909 Hand Clap](http://www.network-909.de/handclap.htm) | Primary circuit description | Topology, envelope story |
| Colin Fraser (synth-diy 2002/2006) | Built both 808 and 909 claps; C140 typo; single vs dual VCA | 808/909 split |
| [Electric Druid, 909 noise](https://electricdruid.net/tr-909-noise-generator/) | 31-stage LFSR, taps discussed (31,13 used here) | 909 noise |
| [firstpr TR-909 sound mods](https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf) | Confirms same pulse+tail idea; 808 analog vs 909 LFSR | Noise difference |
| [Baratatronix 808 clap](https://www.baratatronix.com/blog/cascadia-808-clap-synthesis) | Patch-language restatement of the service notes | Timing numbers |
| [Synthchaser 808 clap mods](https://synthchaser.com/wp-content/uploads/2021/05/Synthchaser-TR-808-Hand-Clap-Mods.pdf) | C140 / C139 / R332 | RC values |
| phosphor-dsp `kit_909.rs` | DSP: BPF + sawtooth gate + 808-style tail | Timing/shape reference, not copied |
| 9W9 / Schwung (GPL) | Circuit-modelled 909, "four echoes 12 ms apart" | Independent check of ~10–12 ms spacing. Not used as source. |
| Hexinverter NeinOhNein Clap | Analog eurorack 909 clone + extra pots | Confirms "Level-only original" |
| Drum9 (FAUST) | Full 909 emulator | Not used; FAUST runtime will not fit NTS-3 |
| Plaits | No dedicated clap engine (snare / analog drums only) | — |
| Typical "808 clap" synths | N filtered noise bursts, often square-gated | Rejected as too generic |

Werner / Abel / Smith published physically-informed 808 **kick** and **cymbal**
models (DAFx-14, ICMC/SMC 2014). There is no equivalent published clap paper
and no Werner clap source.

## What this unit models

Circuit-informed, not a transistor-level WDF (NTS-3 genericfx is 32 KB and
cannot link libm):

```
analog LCG noise (mild 9 kHz colour)  ─┐
31-bit LFSR (taps 31,13)              ─┴─ Y crossfade
        │
        ├─ SVF band-pass "crack"
        └─ SVF band-pass "room"
        │
        Y = 0 (808): one VCA ← (burst + tail)
        Y = 1 (909): crack·burst + room·tail
```

Burst is four sequential falling saws (10→12 ms spacing, last saw ~2× longer).
Tail is a decaying exponential (~100→130 ms), mixed under the burst.
Y also slides BPF centres, Q, spacing, and tail mix along the table above.

Not modelled: BA662 VCA feedthrough clicks, analog clock jitter, every
transistor in the comparator chain, accent-bus current into the VCA IC.

## NTS-3 mapping

Hardware clap has no musical controls, so the pad has to invent a performance
axis.

- **X / DENS** — 16-step Euclidean phrase, rotated so 2 and 4 stay the spine.
  Left = beat 2 only, then 2+4, then quarters, then 8ths, then 16ths.
  Far right adds a 32nd flam on the backbeats.
- **Y / TYPE** — first guess: 808 (bottom) → 909 (top). See alternatives below.
- **TONE / DEC / SNAP** — the three knobs clones always add (BPF, tail, crack/room).

### Other Y candidates (undecided on purpose)

1. **TYPE 808↔909** (current) — matches the brief, and it is the only
   circuit-true axis the two machines actually have.
2. **TONE** — R332-style BPF. Most useful missing knob. Easy to swap onto Y.
3. **DEC / room** — tail amount. House vs dry techno.
4. **SPACING** — how tight the four hands are (C140). Flam ↔ cluster.
5. **SNAP** — burst vs tail mix (the 808 clap trimmer people always ride).

If TYPE feels like a preset morph instead of a performance gesture, put TONE
or SNAP on Y and leave TYPE as an Edit enum.

## Regenerating / listening

```bash
g++ -O2 -std=c++11 -I plugins/hclap/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/hclap/scripts/render_offline_test.cc -o /tmp/hclap_test
/tmp/hclap_test
```

The test checks: four burst peaks on a single hit, ~2 hits/bar at low X,
many hits at high X, 808 vs 909 spectral difference, and no blow-ups.

## Sources

- Roland TR-808 Service Notes, CP / MA (block diagram, sawtooth scope, decay chart)
- Roland TR-909 Service Notes, hand-clap voicing
- [network-909 Hand Clap](http://www.network-909.de/handclap.htm)
- Colin Fraser, synth-diy: C140 = 27 nF; 808 combined CV vs 909 dual VCA
- [Electric Druid, TR-909 noise generator](https://electricdruid.net/tr-909-noise-generator/)
- [firstpr.com.au TR-909 sound mods](https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf)
- [Baratatronix, 808 clap synthesis](https://www.baratatronix.com/blog/cascadia-808-clap-synthesis)
- [Synthchaser TR-808 clap mods](https://synthchaser.com/wp-content/uploads/2021/05/Synthchaser-TR-808-Hand-Clap-Mods.pdf)
- Raygum, *The Roland TR-808 Monograph* (noise transistor, two-path clap)
- phosphor-dsp `synth_909_clap` comments (sawtooth gate, not square gates)

Not affiliated with Roland.
