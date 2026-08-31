# Sample sources

Embedded data is a single 16-bit PCM loop at 24 kHz. The settled DJ-horn tone is stored
as a long seamless loop; the opening pitch drop is a pitch envelope.

| Horn | File | License | Source | Playback |
| --- | --- | --- | --- | --- |
| DJ | `assets/dj-airhorn.wav` | Apache-2.0 | [brendanjryan/airhorn](https://github.com/brendanjryan/airhorn) | loop + pitch env (~+6 semitones → settle) |

The WAV is not committed (see `.gitignore`). Fetch it before regenerating:

```bash
curl -L -o plugins/airhorn/assets/dj-airhorn.wav \
  https://raw.githubusercontent.com/brendanjryan/airhorn/master/assets/airhorn.wav
# sha256 66dc09de689ff2302590c4c75bb5a9de292d5003ceacedb57a1f17524e0510fb
```

Regenerate `dsp/airhorn_pcm.h` after swapping the source WAV:

```bash
python3 plugins/airhorn/scripts/embed_pcm.py \
  --out plugins/airhorn/dsp/airhorn_pcm.h \
  plugins/airhorn/assets/dj-airhorn.wav
```

## Loop conditioning

The recorded horn drifts about 10 cents flat and loses roughly 3 dB of high end over
the sustain, so a loop cut straight out of it restarts on brighter material than it
ended on. That step is what was audible as a click once per loop. `embed_pcm.py`
answers it by picking a loop length whose head and tail still line up (currently
`match=0.87`) and by folding the last 12 fundamental periods back into the loop head
as a crossfade, which is why nothing crossfades at playback time.

Regeneration prints the resulting seam figures; a healthy loop lands below 0 dB:

```
period=79.550 samples (301.70 Hz) crossfade=955 samples
loop start=18640 (777 ms) length=10418 cycles=130.96 match=0.870
seam: sample jump=0.03918 excess over loop interior=-4.62 dB
```

`scripts/fade_experiment.py` renders the loop through a model of the engine and scores
the wrap with spectral flux, the usual click detector. The loop this replaced measured
+3.1 dB there (the wrap was the loudest spectral event in the render); the current one
measures -10.6 dB, i.e. below the tone's own movement.
