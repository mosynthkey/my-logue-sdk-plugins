# my-logue-sdk-plugins

Custom [logue SDK](https://github.com/korginc/logue-sdk) units.

**Under construction.** Hardware transfer and wasm preview are still being tested.

Each plugin lists its targets in `plugin.json`. CI cross-compiles those targets and GitHub Pages hosts the unit files. Web MIDI install is implemented for NTS-1 mkII.

## Layout

```
plugins/<name>/
  plugin.json                 # id, module, targets[]
  dsp/                        # shared algorithm
  targets/<platform>/         # header, unit glue, Makefile
third_party/logue-sdk/
web/                          # Pages UI + SysEx sender
```

You do not duplicate the DSP per device. You do add a thin target adapter (`header.c`, `unit.cc` or v1 `OSC_CYCLE`, Makefile). SDK v1 and v2 APIs are not source-compatible. Module types are not interchangeable (an `osc` is not an NTS-3 `genericfx`).

Plugins:

- **PulseFold** — oscillator, `nts-1_mkii`
- **Echo** — delay, `nts-1_mkii` (`delfx`) and `nts-3_kaoss` (`genericfx`)
- **Shaker** — PhISEM percussion, `nts-1_mkii` (`osc`, note on = shake) and `nts-3_kaoss` (`genericfx`, pad motion = shake). Instrument constants follow STK Shakers (Cook / Scavone); not a copy of STK source.

## Targets

| Platform | Unit | CI build | SysEx load | Notes |
| --- | --- | --- | --- | --- |
| `nts-1_mkii` | `.nts1mkiiunit` | yes (gcc 10.3, Cortex-M7) | yes | Implemented. Header `F0 42 3g 00 01 73`. |
| `nts-3_kaoss` | `.nts3unit` | yes (same M7 toolchain) | yes* | Public MIDI Implementation. `genericfx` only. |
| `nts-1` | `.ntkdigunit` | yes (gcc 5.4 / M4) | yes | v1 API. logue-cli + published MIDI spec. |
| `minilogue-xd` | `.mnlgxdunit` | yes (gcc 5.4 / M4) | yes | v1 API. Same as NTS-1 mkI at binary level. |
| `prologue` | `.prlgunit` | yes (gcc 5.4 / M4) | yes | v1 API. Header `F0 42 3g 00 01 4B`. |
| `microkorg2` | `.mk2unit` | yes (Docker / A7) | no | USB mass storage. |
| `drumlogue` | `.drmlgunit` | yes (Docker / A7) | no | USB mass storage. |

\*NTS-3 SysEx uses the same USER SLOT DATA style as mkII. Family ID must be taken from its MIDI Implementation; not wired in `web/` yet.

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
