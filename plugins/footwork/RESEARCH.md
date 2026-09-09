# Footwork Research Notes

Tempo-synced Chicago footwork / juke phrase pad for NTS-3. Sparse kick spine
plus X-driven stutter kicks; Y grows snare rolls.

## Pattern spine (16 steps)

- Kick spine: `0`, `3`, `8`, `11`
- Stutter seats: remaining 16ths filled by X probability
- Snare: `4`, `12` plus roll seats near bar ends
- Default BPM: 160; short bodies so hits stay crisp

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / STUT | Kick stutter density |
| Y / ROLL | Snare rolls + hat frenzy; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/footwork/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/footwork/scripts/render_offline_test.cc -o /tmp/footwork_test
/tmp/footwork_test
```
