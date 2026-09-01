import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { japanesePluginDescriptions } from "../website/src/composables/useI18n.js";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

test("every plugin has a Japanese description", async () => {
  const pluginNames = await readdir(path.join(repoRoot, "plugins"));

  for (const pluginName of pluginNames) {
    const pluginPath = path.join(repoRoot, "plugins", pluginName, "plugin.json");
    let plugin;

    try {
      plugin = JSON.parse(await readFile(pluginPath, "utf8"));
    } catch (error) {
      if (error.code === "ENOENT") continue;
      throw error;
    }

    assert.ok(
      japanesePluginDescriptions[plugin.id],
      `${plugin.id} must have a Japanese description`,
    );
  }
});
