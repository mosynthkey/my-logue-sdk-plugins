# Repository Guidelines

## Adding and Previewing a Plugin Locally

Use an existing plugin with a similar target as the starting point:

- `plugins/fbackosc/` for an NTS-1 mkII keyboard oscillator.
- `plugins/shaker/` for a plugin shared by NTS-1 mkII and NTS-3.
- `plugins/kaocid/` for an NTS-3 XY-pad effect.
- `plugins/hypersaw/targets/microkorg2/` for a microKORG2 target.

A typical plugin has this structure:

```text
plugins/myplugin/
├── plugin.json
├── dsp/
│   └── myplugin.h
└── targets/
    └── nts-1_mkii/
        ├── config.mk
        ├── header.c
        ├── unit.cc
        ├── wasm.cc
        └── Makefile
```

Define the catalog metadata in `plugin.json`. Mark work-in-progress plugins with
`"experimental": true`; they can then be viewed locally with
`http://localhost:5173/?experimental` without appearing in the normal public
list.

Set the project name and module type in the target's `config.mk`. For example:

```makefile
PROJECT := myplugin
PROJECT_TYPE := osc

UCSRC = header.c
UCXXSRC = unit.cc
UINCDIR = ../../dsp
ULIBS = -lm
```

NTS-3 targets normally use `PROJECT_TYPE := genericfx`.

Build the hardware unit for one target from the repository root:

```bash
make -C plugins/myplugin/targets/nts-1_mkii install \
  GCC_BIN_PATH=/path/to/gcc-arm-none-eabi/bin
```

This must produce a unit file directly inside the target directory, such as
`myplugin.nts1mkiiunit` or `myplugin.nts3unit`. A plugin without a built unit is
omitted when local preview assets are synchronized.

Build the WebAssembly preview when the target provides `wasm.cc`:

```bash
make -C plugins/myplugin/targets/nts-1_mkii wasm-ci \
  EMCC_BIN_PATH=/path/to/emsdk/upstream/emscripten
```

The WASM build writes its files to the target's `sim/` directory. A plugin may
still appear in the website without a WASM build, but it will not have an audio
preview.

Synchronize built units, simulator files, and plugin metadata into the local
website:

```bash
bash scripts/sync-website-preview.sh
```

Then start the website development server:

```bash
cd website
npm install
npm run dev
```

Keep the development server running while iterating. After changing DSP or WASM
code, rebuild the hardware unit and WASM target, run
`scripts/sync-website-preview.sh` again, and reload the browser.

The complete iteration loop is:

```bash
make -C plugins/myplugin/targets/nts-1_mkii install \
  GCC_BIN_PATH=/path/to/gcc-arm-none-eabi/bin

make -C plugins/myplugin/targets/nts-1_mkii wasm-ci \
  EMCC_BIN_PATH=/path/to/emsdk/upstream/emscripten

bash scripts/sync-website-preview.sh
```

Use `website/` for editable web application source. `website/dist/` is Vite's
temporary production build output, while `dist/website/` is the assembled,
gitignored deployment artifact.

## NTS-3 Hardware Pitfalls (HSnare / SnareRush notes)

Symptom “only an attack click / プツ on the device” is **not always** a link /
Resolve Symbol failure. Check both.

### 1. Unresolved libm (real link / load failure)

NTS-3 does **not** export `sinf` / `cosf` / `expf` / `powf` / `exp2f` / …
to unit ELFs. If the unit leaves those as undefined, load can fail with
Resolve Symbol, or pitch/filter math collapses to clicks (see Kaocid fix
`bba9593`, TransitionLooper `-lm` fix `#73`).

How to verify after `make … install`:

```bash
arm-none-eabi-nm -u plugins/myplugin/targets/nts-3_kaoss/build/myplugin.elf
arm-none-eabi-readelf -r plugins/myplugin/targets/nts-3_kaoss/build/myplugin.elf
```

Prefer header-only `utils/float_math.h` (`faster*` / `si_fabsf`) and keep
`plugins/common/fx_dsp.h` libm-free. If a true libm call is unavoidable,
set `ULIBS = -lm` in `config.mk` so the code is linked into the unit.

### 2. HSnare / SnareRush (2026-09): not a link error

Built `hsnare.elf` and `snarerush.elf` have **no undefined libm symbols**.
PLT entries are only local methods + `memset`. SnareRush already has
`ULIBS = -lm`; HSnare has `ULIBS =` empty. Link resolution is fine.

Root cause of the click-only sound:

- **`fasterexpf` is unusable for per-sample envelope coefficients near 1.**
  Mineiro’s approx is biased around 0: `fasterexpf(0) ≈ 0.971` (not 1.0).
  HSnare `envCoeff(seconds)` does `fasterexpf(-1/(seconds*48000))` with
  argument ≈ `-2e-4`. That yields coeff ≈ `0.971` instead of ≈ `0.99975`,
  so a supposed ~85 ms body dies in ~5 ms → attack click only.
  Host-side single-trigger probe (SNAP=0): peak ~0.09, nearly silent by 20 ms.

- **SnareRush** uses hardcoded multiply decays `0.988` / `0.992`
  (~12–18 ms to −60 dB). Each hit is already a short click; a roll is a
  train of clicks. Also not a link issue.

Safer patterns for amp envelopes on NTS-3:

- Prefer age-based level: `level = fasterexpf(-age / tau)` with `|age/tau|`
  not ≪ 1 for most of the note (HClap-style), **or**
- For per-sample coeffs near 1, avoid `fasterexpf`; use a linearization
  such as `1.f + x` when `|x|` is tiny (`x = -1/(seconds*sr)`), or a
  better `exp` approx (`fastexpf` / table), and always probe decay length
  on the host before flashing.

### 3. Quick triage checklist

1. `nm -u` / `readelf -r` → any `sinf`/`expf`/… UND? → link/libm problem.
2. No UND, but body under ~20 ms on a host offline render → envelope /
   `fasterexpf` misuse (HSnare/SnareRush class), not the loader.
3. Kaocid-style: also confirm touch clock (prefer internal sample clock over
   relying only on host `tempo4ppqnTick` if ticks are sparse).
