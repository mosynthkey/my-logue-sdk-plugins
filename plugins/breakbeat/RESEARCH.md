# BreakBeat Research Notes

Tempo-synced Amen-*inspired* synthetic breakbeat for NTS-3. Not a sample of
the Winstons recording (see AmenTime for a slicer). Voices are synthesized.

## Pattern spine (16 steps)

- Kick: `0`, `2`, `6`, `10`
- Snare: `4`, `12` plus secondary seats `7`, `13`, `15`
- Ghost seats fill syncopation between spine hits
- Default BPM: 174 (jungle); works at ~130 for breaks

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / SYNC | Syncopation / ghost + secondary snare density |
| Y / ENERGY | Break energy toward Fill; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/breakbeat/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/breakbeat/scripts/render_offline_test.cc -o /tmp/breakbeat_test
/tmp/breakbeat_test
```
