#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-"$ROOT/dist/website"}"

rm -rf "$DEST"
mkdir -p "$DEST/units" "$DEST/sim" "$DEST/vendor"

(
  cd "$ROOT/website"
  if [ -f package-lock.json ]; then
    npm ci
  else
    npm install
  fi
  npm run build
)

cp -r "$ROOT/website/dist/." "$DEST/"
cp "$ROOT/website/styles.css" "$ROOT/website/nts1-midi.js" "$DEST/"
cp "$ROOT/website/vendor/coi-serviceworker.js" "$ROOT/website/vendor/qwerty-hancock.js" "$DEST/vendor/"
cp "$ROOT/website/vendor/coi-serviceworker.js" "$DEST/"

python3 - "$ROOT" "$DEST" <<'PY'
import json
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path

UNIT_SUFFIXES = (
    ".nts1mkiiunit",
    ".ntkdigunit",
    ".prlgunit",
    ".mnlgxdunit",
    ".nts3unit",
    ".drmlgunit",
    ".mk2unit",
)

root = Path(sys.argv[1])
dest = Path(sys.argv[2])
plugins = []

for plugin_json in sorted((root / "plugins").glob("*/plugin.json")):
    plugin_dir = plugin_json.parent
    meta = json.loads(plugin_json.read_text())
    builds = []

    for target_dir in sorted((plugin_dir / "targets").glob("*")):
        if not target_dir.is_dir():
            continue
        unit_files = [path for path in target_dir.iterdir() if path.suffix in UNIT_SUFFIXES]
        if not unit_files:
            raise SystemExit(f"Missing unit file in {target_dir}")
        unit_path = unit_files[0]
        dest_name = f"{plugin_dir.name}-{target_dir.name}{unit_path.suffix}"
        (dest / "units" / dest_name).write_bytes(unit_path.read_bytes())

        module = None
        config_mk = target_dir / "config.mk"
        if config_mk.is_file():
            for line in config_mk.read_text().splitlines():
                stripped = line.strip()
                if stripped.startswith("PROJECT_TYPE"):
                    module = stripped.split("=", 1)[-1].strip()
                    break

        wasm_href = None
        sim_src = target_dir / "sim"
        if sim_src.is_dir():
            sim_dest = dest / "sim" / plugin_dir.name / target_dir.name
            sim_dest.mkdir(parents=True, exist_ok=True)
            for child in sim_src.iterdir():
                target = sim_dest / child.name
                if child.is_file():
                    target.write_bytes(child.read_bytes())
                elif child.is_dir():
                    shutil.copytree(child, target, dirs_exist_ok=True)
            (sim_dest / "coi-serviceworker.js").write_bytes(
                (root / "website/vendor/coi-serviceworker.js").read_bytes()
            )
            html_files = list(sim_dest.glob("*.html"))
            if html_files:
                html = html_files[0]
                text = html.read_text()
                snippet = '<script src="./coi-serviceworker.js"></script>\n'
                if "coi-serviceworker" not in text:
                    html.write_text(text.replace("<head>", "<head>\n  " + snippet, 1))
                wasm_href = f"./sim/{plugin_dir.name}/{target_dir.name}/{html.name}"

        builds.append(
            {
                "target": target_dir.name,
                "module": module,
                "file": f"./units/{dest_name}",
                "size": unit_path.stat().st_size,
                "wasm": wasm_href,
            }
        )

    plugins.append({**meta, "builds": builds})

catalog = {"generatedAt": datetime.now(timezone.utc).isoformat(), "plugins": plugins}
(dest / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n")
(dest / ".nojekyll").write_text("")
print(f"Assembled {len(plugins)} plugin(s) into {dest}")
PY
