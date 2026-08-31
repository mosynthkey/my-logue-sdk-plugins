# Sample sources

Embedded data is a single 16-bit PCM loop at 24 kHz. The settled DJ-horn tone is stored
as a long seamless loop; the opening pitch drop is a pitch envelope.

| Horn | File | License | Source | Playback |
| --- | --- | --- | --- | --- |
| DJ | `assets/dj-airhorn.wav` | Apache-2.0 | [brendanjryan/airhorn](https://github.com/brendanjryan/airhorn) | loop + pitch env (~+6 semitones → settle) |

Regenerate `dsp/airhorn_pcm.h` after swapping the source WAV:

```bash
python3 plugins/airhorn/scripts/embed_pcm.py \
  --out plugins/airhorn/dsp/airhorn_pcm.h \
  plugins/airhorn/assets/dj-airhorn.wav
```
