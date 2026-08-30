# Sample sources

Embedded data is 16-bit PCM at 24 kHz. DJ and TRAIN store a short seamless loop of the
settled pitch; the opening pitch drop is a pitch envelope. BIKE is a one-shot honk.

| Horn | File | License | Source | Playback |
| --- | --- | --- | --- | --- |
| DJ | `assets/dj-airhorn.wav` | Apache-2.0 | [brendanjryan/airhorn](https://github.com/brendanjryan/airhorn) | loop + pitch env (~+6 semitones → settle) |
| TRAIN | `assets/leslie-airhorn.wav` | CC0 | [Wikimedia: Leslie A200-156](https://commons.wikimedia.org/wiki/File:Leslie_A200-156.ogg) | loop |
| BIKE | `assets/bicycle-horn.wav` | CC0 | [OpenGameArt: Bicycle Horn](https://opengameart.org/content/bicycle-horn) | one-shot |

Regenerate `dsp/airhorn_pcm.h` after swapping source WAVs:

```bash
python3 plugins/airhorn/scripts/embed_pcm.py \
  --out plugins/airhorn/dsp/airhorn_pcm.h \
  plugins/airhorn/assets/dj-48k.wav \
  plugins/airhorn/assets/leslie-airhorn.wav \
  plugins/airhorn/assets/bicycle-48k.wav
```
