# airFM

Two-operator phase-modulation FM for the Nu:Tekt NTS-3 kaoss pad kit, inspired by Alesis airSynth Program 3 "FM". Load as a **genericfx** unit; it ignores audio input and acts as a touch-controlled oscillator.

## Specification

1. f1 map: 40–8000 Hz log (pad X)
2. f2 map: 55–550 Hz log (pad Y; YMAX knob sets top to 400–800 Hz)
3. INDEX=4.0, DRIVE=1.08, AMP/LEVEL=0.28, LPF=7500 Hz, HPF=18 Hz, FB=0

## Pad control

Touch coordinates from the SDK are **1024×1024**, origin bottom-left (`runtime.h`). They are normalized to 0..1 before frequency mapping.

| Axis | Map | Range |
|------|-----|-------|
| X | `f1 = 40 * 200^x` | 40–8000 Hz |
| Y | `f2 = 55 * (YMAX/55)^y` | 55–YMAX Hz (default YMAX 550) |

- **Touch began / moved:** update f1/f2, gate on.
- **Touch ended / cancelled:** gate off; 10 ms linear fade on LEVEL only. Frequencies hold at last XY.

## Parameters

| # | Name | Default | Range | Notes |
|---|------|---------|-------|-------|
| 1 | INDEX | 4.00 | 0.50–8.00 rad | FM index |
| 2 | YMAX | 550 Hz | 400–800 Hz | Modulator log top |
| 3 | DRIVE | 1.08 | 0.70–1.60 | tanh drive |
| 4 | LEVEL | 0.28 | 0.10–0.40 | Output while touching |

Pad XY and touch gate are the only required live inputs. No Z/proximity volume.

## FX depth (dry/wet)

On NTS-3, the **DEPTH** control mixes dry input with the effect output. This unit generates audio without using the input — set **DEPTH to 100% wet** so the oscillator is audible.

## Build

From the repository root (after submodules are initialized):

```bash
git submodule update --init
git -C third_party/logue-sdk submodule update --init platform/ext/CMSIS

make -C plugins/airfm/targets/nts-3_kaoss install \
  GCC_BIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin
```

Or build all plugins:

```bash
make GCC_BIN_PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin
```

Output: `plugins/airfm/targets/nts-3_kaoss/airfm.nts3unit`

WebAssembly preview (optional):

```bash
make -C plugins/airfm/targets/nts-3_kaoss wasm-ci \
  EMCC_BIN_PATH=/path/to/emsdk/upstream/emscripten
```

## Algorithm

```
mod = sin(ph_m + FB * mod)     // FB = 0
s   = tanh(DRIVE * sin(ph_c + INDEX * mod))
v   = LEVEL * s
DC block + 1-pole LPF (18 Hz HPF, 7500 Hz LPF)
L = R
```

48 kHz, block processing, no FFT/delay/oversampling.

## License

BSD 3-Clause. logue-sdk remains KORG's BSD 3-Clause.
