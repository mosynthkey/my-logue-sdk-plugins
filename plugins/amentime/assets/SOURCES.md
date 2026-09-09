# AmenTime sample source

The embedded break is an original synthesized 1-bar drum pattern in the common
amen chop map (16 equal 16ths). It is **not** the Winstons *Amen, Brother*
recording.

Regenerate `dsp/amentime_pcm.h`:

```bash
python3 plugins/amentime/scripts/synth_amen.py \
  --out plugins/amentime/dsp/amentime_pcm.h
```

The renderer writes 12 kHz 8-bit PCM so the bar fits the NTS-3 genericfx size
budget. Playback rate follows host BPM, so 136 BPM is original pitch.
