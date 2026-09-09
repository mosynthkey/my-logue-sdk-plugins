# AmenWav sample source

AmenWav slices a **local 1-bar WAV**. Attribution / a copyright notice does **not**
grant a license to redistribute The Winstons' *Amen, Brother* recording, so that
file is never fetched or committed here.

## Public / CI build

If `assets/break.wav` is missing, `scripts/embed_wav.py` writes an original
click grid so the unit still compiles. That placeholder is not an amen break.

## Personal build with your own break

```bash
# 1-bar WAV, typically ~136 BPM. Not committed (.gitignore).
cp /path/to/your-break.wav plugins/amenwav/assets/break.wav

python3 plugins/amenwav/scripts/embed_wav.py \
  --wav plugins/amenwav/assets/break.wav \
  --bpm 136 \
  --start 0 \
  --out plugins/amenwav/dsp/amenwav_pcm.h

make -C plugins/amenwav/targets/nts-3_kaoss install \
  GCC_BIN_PATH=/path/to/gcc-arm-none-eabi/bin
```

`--start` is seconds into the file (use it to pick one bar of a longer break).
Do **not** commit `assets/break.wav` or PCM generated from a recording you
cannot redistribute.
