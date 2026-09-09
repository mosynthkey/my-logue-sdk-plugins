# Dembow Research Notes

Tempo-synced reggaeton dembow phrase pad for NTS-3. The kick/cha spine is
fixed; X grows rim answers.

## Pattern spine (16 steps)

- Kick: `0`, `6`, `8`, `14`
- Snare: `4`, `12` plus dembow cha on `7`, `15`
- Rim seats: soft woodblock-ish answers between spine hits
- Default BPM: 96

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / RIM | Rim / cha answer density |
| Y / PERC | Hats / percussion + fill; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/dembow/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/dembow/scripts/render_offline_test.cc -o /tmp/dembow_test
/tmp/dembow_test
```
