# HHat Research and Implementation Notes

NTS-3 phrase pad for a **TR-909 hi-hat PCM circuit model**, not 808
metal squares and not a modern WAV player. Classical machines expose
Closed and Open as two keys; this unit puts the continuum on Y.

## What the 909 hi-hat actually does

Roland Service Notes + Network-909 + Colin Fraser:

```
NAND astable (~60 kHz) → ÷2 → address counters
      ↓
HN61256P C43  (32 KB shared CH/OH)
      ↓
6-bit latch → resistor DAC
      ↓
analog decay VCA (CH / OH Decay pots) + anti-log
      ↓
two reconstruction LPFs → level
```

| | Closed | Open |
| --- | --- | --- |
| ROM window | `0x6000..0x7FFF` (top ¼) | `0x0000..0x5FFF` (bottom ¾) |
| Selection | CLOSED line ORs top address bits to `11` | counters run until top bits hit `11` |
| Envelope | analog RC decay (not address DAC) | same, longer pot range |
| Clock | fixed ~30 kHz (no Tune knob) | same |

Ride/Crash use address-derived anti-log envelopes so Tune shortens
decay. Hats do **not** — Decay pots restore the compressed PCM.

### ROM provenance

| | |
| --- | --- |
| MAME | `hn61256p__c43.ic69` |
| CRC32 | `2aaae11b` |
| SHA1 | `22a34d603e78673bb096f5e56a8971afe14d8fee` |
| Working dump | Colin Fraser `r909hh.wav` (8-bit left-aligned 6-bit @ 32 kHz, 32768 frames) |

Matches Ride909’s packing: 4 samples → 3 bytes, ~24.6 KB in the unit.

## Experiments (Close–Open morph)

Offline A/B on the real ROM (see `/tmp/hhat_exp_*.wav` during bring-up):

1. **CH/OH crossfade + τ(Y)** — closed punches from CH window, open
   blooms from OH, half-open keeps body. Chosen.
2. **OH-only + τ(Y)** — continuous but loses the tight CH stick.
3. **Start-address lerp** — mid-Y lands in quiet mid-OH; weaker.

Also compared earlier 808 six-square metal model: too “buzz saw”, not
909 grit. PCM + ZOH + 6-bit DAC + dual LPF is the character.

## What this unit models

```
30 kHz ZOH (TUNE ±7 st) → packed 6-bit ROM
    ├─ CH reader @ 0x6000
    └─ OH reader @ 0x0000
         → equal-power-ish crossfade from Y
         → age-based decay VCA  τ(Y, DEC)   [fasterexpf(-age/τ)]
         → reconstruction LPF A (TONE) + LPF B
         → DC block
```

- **X = DENS**: `fx::euclidHit` on 16 steps from step 0; high dens → 32nd rolls (closed-biased).
- **Y = OPEN**: CH↔OH window mix + τ; foot choke when Y drops or a more-closed hit arrives.
- **No libm**: packed PCM, baked LPF coeffs, `exp2Approx` for Tune, age-based env only.

## Size

Packed PCM ≈ 24.6 KB. Keep the phrase engine lean (6 voices) so the
stripped unit stays under the 32 KB genericfx cap (same constraint as
Ride909).

## Regenerating the PCM header

```bash
python3 plugins/hhat/scripts/embed_rom.py \
  --rom /path/to/hn61256p__c43.bin \
  --out plugins/hhat/dsp/hhat_pcm.h
```

Or from Fraser’s WAV (mono 8-bit, 32768 frames): write the raw frames
as a 32768-byte `.bin` first.

## Sources

- Network-909, *HiHat and Cymbals*
- Colin Fraser, synth-diy: CH = top ¼, OH = bottom ¾; `r909hh.wav`
- Raygum, *The Roland TR-909 Monograph*
- Ride909 unit in this repo (ROM pack / ZOH / LPF path)
- MAME `roland_tr909.cpp` ROM hashes

Not affiliated with Roland.
