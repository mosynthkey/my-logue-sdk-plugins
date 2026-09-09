# Trance Research Notes

Tempo-synced trance drum phrase pad for NTS-3.

## Voices (TR-909 split)

| Voice | Source |
| --- | --- |
| Closed / Open hats | Packed 6-bit PCM from TR-909 Hi-Hat ROM (HN61256P C43), same dump as Trap808 / HHat |
| Kick (BD) | Analog circuit model — triangle VCO → soft tanh sine shaper, pitch sweep, short noise click |
| Clap | Analog circuit model — 31-stage LFSR noise, sawtooth burst train + room tail (HClap 909 path) |

### Why kick/clap are models, not ROM

Roland never sampled the 909 bass drum or hand clap. Service notes:

- BD / SD / toms / rim / clap = analog voice board
- Hats / ride / crash = 6-bit PCM ROMs

So hats can be “taken from the 909” as ROM dumps; BD and clap have to be circuit models (same approach as HClap / HSnare). Sample packs are one-shots, not original material.

Hat playback: 30 kHz ZOH → 6-bit DAC mid/scale → light LPF. Closed chokes open.
Near-1 hat envelopes use `1 + x` linearization (not `fasterexpf`).

## Pattern spine (16 steps)

- Kick: `0`, `4`, `8`, `12`
- Clap: `4`, `12`
- Open hats: `2`, `6`, `10`, `14`
- Default BPM: 138

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / HATS | Offbeat open energy + closed density (909 ROM) |
| Y / BUILD | Clap-roll build toward Fill; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/trance/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/trance/scripts/render_offline_test.cc -o /tmp/trance_test
/tmp/trance_test
```
