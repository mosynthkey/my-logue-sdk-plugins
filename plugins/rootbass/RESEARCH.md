# RootBass Research Notes

Lowest-pitch follower bass for NTS-3. Audio In is low-passed and pitch-tracked
with AMDF in the bass band (≈40–220 Hz), then quantized to 12-TET A440.

## Rhythm: Tresillo

Featured gate pattern is the Latin **Tresillo** cell (3+3+2), the same
subdivision that underpins dembow / reggaeton grooves:

| Half-bar | Steps (16th) |
| --- | --- |
| Beat 1–2 | 0, 3, 6 |
| Beat 3–4 | 8, 11, 14 |

Pitch assumption: the input chord is stable **inside** one 16th-note step.
The voice latches the detector on each step boundary, so it can follow chord
changes from step to step, but does not hunt mid-step. `HOLD` follows the
detector continuously. Also included:

- `HOLD` — continuous drone while the pad is held
- `CINQ` — Cinquillo (2+1+2+1+2) for denser Afro-Cuban gating
- `HALF` — hits on 0 and 8 only

## Mapping

| Control | Role |
| --- | --- |
| Hold pad | Gate / run — steps lock to host 4ppqn |
| X / OCT | Octave offset (−2…+1) |
| Y / WAVE | SIN / TRI / SAW / SQR |
| Depth / MIX | Bass over Audio In |
| RHY | HOLD / TRESI / CINQ / HALF |
| DEC | Amp decay after each hit |

Default BPM ≈ 96 (tresillo-friendly).

## Host offline test

```bash
g++ -O2 -std=c++11 -I plugins/rootbass/dsp -I plugins/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss/common \
  -I third_party/logue-sdk/platform/nts-3_kaoss \
  plugins/rootbass/scripts/render_offline_test.cc -o /tmp/rootbass_test
/tmp/rootbass_test
```
