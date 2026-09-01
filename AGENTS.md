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
