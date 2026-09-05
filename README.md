# my-logue-sdk-plugins

Custom [logue SDK](https://github.com/korginc/logue-sdk) units.

**Under construction.** Hardware transfer and wasm preview are still being tested.

Each plugin lists its targets in `plugin.json`. CI cross-compiles those targets and GitHub Pages hosts the unit files. Web MIDI install is implemented for NTS-1 mkII and NTS-3.

## Layout

```
plugins/<name>/
  plugin.json                 # id, module, targets[]
  dsp/                        # shared algorithm
  targets/<platform>/         # header, unit glue, Makefile
third_party/logue-sdk/
website/                      # Web app source: Pages UI + SysEx sender
dist/website/                 # Generated deploy artifact (gitignored)
```

You do not duplicate the DSP per device. You do add a thin target adapter (`header.c`, `unit.cc` or v1 `OSC_CYCLE`, Makefile). SDK v1 and v2 APIs are not source-compatible. Module types are not interchangeable (an `osc` is not an NTS-3 `genericfx`).

Plugins:

- **HyperSaw** — Virus TI-inspired 9-voice detuned saw stack with Density, Spread, HyperSub, and stereo width. Targets `nts-1_mkii` and `microkorg2`.
- **FbOsc** — JP-8080-inspired Feedback oscillator (band-limited saw through a key-tracked resonant comb filter). Targets `nts-1_mkii` and `microkorg2`.
- **Ride909** — NTS-3 techno ride wash (off-beats 3-7-11-15, kick pump). Voice is the TR-909 Ride ROM (6-bit PCM) through variable-rate playback, resistor DAC, and analog reconstruction, not a WAV sampler.
- **Shaker** — PhISEM percussion, `nts-1_mkii` (`osc`, note on = shake) and `nts-3_kaoss` (`genericfx`, pad motion = shake). Instrument constants follow STK Shakers (Cook / Scavone); not a copy of STK source.
- **airFM** — two-op phase-mod FM, `nts-3_kaoss` (`genericfx`, pad XY = carrier/modulator, touch gate). Inspired by Alesis airSynth Program 3.
- **AirHorn** — DJ air horn at the original recorded pitch. A pitch envelope recreates the opening drop, then a long 16-bit loop holds the settled tone. Targets `nts-1_mkii`, `nts-3_kaoss`, and `microkorg2`.
- **Kaocid** — TB-303 style acid bass with auto phrase generator, `nts-3_kaoss` (`genericfx`, hold pad = tempo-synced 16-step pattern with glides, retouch = new phrase). Panel: Cutoff, Resonance, Wave, Env Mod, Decay, Accent, plus ROOT and Mix. Voice inspired by gsynth TB-303 (Andy Sloane, 2001).
- **TechnoRumble** — Techno rumble kick processor (`revfx` on mkII, `genericfx` on NTS-3): long reverb tail, sub LPF, drive, and transient-triggered sidechain duck. Feed a kick on AUDIO IN or synth output.
- **TransitionLooper** — NTS-3 DJ transition looper: captures AUDIO IN into a tempo-synced 16-step stereo loop plus wrap glue. Prefers `get_raw_input` (firmware 1.4+) while the pad is up; if that pre-roll is silent, the first hold records one live bar and then loops. Pad up bypasses; hold fades into the stored loop (volume, HPF/LPF, bass swap, echo out, brake, or roll).

## Targets

| Platform | Unit | CI build | SysEx load | Notes |
| --- | --- | --- | --- | --- |
| `nts-1_mkii` | `.nts1mkiiunit` | yes (gcc 10.3, Cortex-M7) | yes | Implemented. Header `F0 42 3g 00 01 73`. |
| `nts-3_kaoss` | `.nts3unit` | yes (same M7 toolchain) | yes | Header `F0 42 3g 00 01 72`. `genericfx` only, 50 slots. |
| `nts-1` | `.ntkdigunit` | yes (gcc 5.4 / M4) | yes | v1 API. logue-cli + published MIDI spec. |
| `minilogue-xd` | `.mnlgxdunit` | yes (gcc 5.4 / M4) | yes | v1 API. Same as NTS-1 mkI at binary level. |
| `prologue` | `.prlgunit` | yes (gcc 5.4 / M4) | yes | v1 API. Header `F0 42 3g 00 01 4B`. |
| `microkorg2` | `.mk2unit` | yes (Docker / A7) | no | USB mass storage → `Units/Oscs/SLOTxx/`. FW >= 2.0. |

### microKORG2 install (HyperSaw / FbOsc)

1. Download the **`microkorg2`** build (`.mk2unit`), not **mkII** (`.nts1mkiiunit`).
2. Power off, hold **FUNCTION 1**, power on → USB mass storage mode.
3. Copy the file into an empty folder under **`Units/Oscs/`** (one unit per SLOT).
4. Eject, press **FUNCTION 5**, then pick the unit on an **OSC1/2/3** page.

Web SysEx send does **not** work on microKORG2.
| `drumlogue` | `.drmlgunit` | yes (Docker / A7) | no | USB mass storage. |

v1 units (prologue, minilogue xd, NTS-1 mkI) are binary-compatible with each other. Nothing else is.

## Commands

```bash
git submodule update --init
git -C third_party/logue-sdk submodule update --init platform/ext/CMSIS

make GCC_BIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin
make test
make wasm EMCC_BIN_PATH=/path/to/emsdk/upstream/emscripten
```

Do not `git submodule update --recursive` (pulls the huge emsdk tree).

## License

BSD 3-Clause. logue-sdk remains KORG’s BSD 3-Clause. Not affiliated with KORG.
