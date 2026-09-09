# Trance Research Notes

Tempo-synced trance drum phrase pad for NTS-3. Four-on-the-floor + offbeat
open hats are the identity.

## Pattern spine (16 steps)

- Kick: `0`, `4`, `8`, `12` (four-on-the-floor)
- Clap/snare: `4`, `12` (beats 2 and 4)
- Open hats: `2`, `6`, `10`, `14` (classic offbeats)
- Default BPM: 138

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / HATS | Offbeat open energy + closed density |
| Y / BUILD | Snare-roll build toward Fill; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/trance/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/trance/scripts/render_offline_test.cc -o /tmp/trance_test
/tmp/trance_test
```
