import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const pluginsRoot = path.join(repoRoot, "plugins");

test("every NTS-3 preview exposes a Depth mapping", async () => {
  const pluginNames = await readdir(pluginsRoot);
  const headers = [];

  for (const pluginName of pluginNames) {
    const headerPath = path.join(
      pluginsRoot,
      pluginName,
      "targets",
      "nts-3_kaoss",
      "header.c",
    );

    try {
      headers.push([pluginName, await readFile(headerPath, "utf8")]);
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
    }
  }

  assert.ok(headers.length > 0, "expected at least one NTS-3 plugin header");

  for (const [pluginName, header] of headers) {
    assert.match(
      header,
      /\.default_mappings\s*=\s*\{[\s\S]*?k_genericfx_param_assign_depth/,
      `${pluginName} must assign one default mapping to Depth`,
    );
  }
});
