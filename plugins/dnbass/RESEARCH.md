# DnBass Research Notes

Tempo-synced drum & bass phrase pad for NTS-3. Half-time snare on beat 3
is the identity; X rolls the hats.

## Pattern spine (16 steps)

- Kick: `0`, `6`, `10` (downbeat + syncopated pickups)
- Snare: `8` (half-time on 3)
- Hats: densifying 16ths via X
- Default BPM: 174

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate only — steps lock to host 4ppqn |
| X / HATS | Rolling hat density |
| Y / BREAK | Ghost kicks/snares + fill; top-right flick = 1-bar Fill |

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/dnbass/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/dnbass/scripts/render_offline_test.cc -o /tmp/dnbass_test
/tmp/dnbass_test
```
