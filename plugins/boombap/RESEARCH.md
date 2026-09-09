# BoomBap Research Notes

Tempo-synced boom-bap phrase pad for NTS-3. Dusty ghosts around a hard
backbeat are the point — not four-on-the-floor.

## Pattern spine (16 steps)

- Kick: `0`, `7`, `10` (boom on 1, late push into 3)
- Snare: `4`, `12` (hard 2 and 4)
- Ghost seats: soft snares/rims before/after the backbeat
- Default BPM: 90; SWING maps toward heavy MPC shuffle

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / GHOST | Ghost density |
| Y / HATS | Hat density + fill energy; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/boombap/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/boombap/scripts/render_offline_test.cc -o /tmp/boombap_test
/tmp/boombap_test
```
