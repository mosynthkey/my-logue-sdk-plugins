# Sample sources

Embedded samples are trimmed to ~650 ms and stored as 8-bit PCM at 22050 Hz.

| Horn | File | License | Source |
| --- | --- | --- | --- |
| DJ | `assets/dj-airhorn.wav` | Apache-2.0 | [brendanjryan/airhorn](https://github.com/brendanjryan/airhorn) |
| TRAIN | `assets/leslie-airhorn.wav` | CC0 | [Wikimedia: Leslie A200-156](https://commons.wikimedia.org/wiki/File:Leslie_A200-156.ogg) |
| BIKE | `assets/bicycle-horn.wav` | CC0 | [OpenGameArt: Bicycle Horn](https://opengameart.org/content/bicycle-horn) |

Regenerate `dsp/airhorn_pcm.h` after editing source WAVs:

```bash
python3 plugins/airhorn/scripts/embed_pcm.py \
  --out plugins/airhorn/dsp/airhorn_pcm.h \
  plugins/airhorn/assets/dj-48k.wav \
  plugins/airhorn/assets/leslie-airhorn.wav \
  plugins/airhorn/assets/bicycle-48k.wav
```
