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

The same sustain also has a slow loudness swell of about 1.3 dB peak-to-peak. Locked
into a 434 ms loop that swell repeats at ~2.3 Hz and reads as an amp LFO, so after the
crossfade the extractor divides out a short circular RMS envelope (2-period window,
4-period Hann smooth). That drops the residual swell to about 0.07 dB in the PCM and
about 0.7 dB once rendered through the engine.

Regeneration prints the resulting seam figures; a healthy loop lands below 0 dB on the
seam excess and well under 0.5 dB on the level swell:

```
period=79.550 samples (301.70 Hz) crossfade=955 samples
loop start=18640 (777 ms) length=10418 cycles=130.96 match=0.870
level flatten: swell 1.29 dB -> 0.07 dB (removed 1.22 dB)
seam: sample jump=0.04058 excess over loop interior=-4.23 dB level swell=0.07 dB
```

`scripts/fade_experiment.py` renders the loop through a model of the engine and scores
the wrap with spectral flux, the usual click detector. The loop this replaced measured
+3.1 dB there (the wrap was the loudest spectral event in the render); the current one
measures around -10 dB, i.e. below the tone's own movement.

### Runtime dual-player crossfade

A ping-pong pair that crossfades near every loop boundary is the usual sampler fix when
the PCM still has a hard seam. Here the seam is already baked into the loop head, so a
second player would crossfade the raw tail into material that already contains that
tail. That re-introduces level pumping instead of removing it.

```bash
python3 plugins/airhorn/scripts/fade_experiment.py --compare-loop-modes
```

On the shipping baked loop this reports about **0.37 dB** short-window RMS swell for
`single_wrap` versus about **2.9 dB** for `dual_crossfade`, while the wrap seam score
also gets worse. Keep the single wrapping player at playback time; fix seams and slow
swell in `embed_pcm.py` instead.
