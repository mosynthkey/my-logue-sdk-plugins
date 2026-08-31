import assert from "node:assert/strict";
import test from "node:test";
import {
  findPlugin,
  isExperimentalEnabled,
  resolveInitialPlugin,
  visiblePlugins,
} from "../web/src/utils/visiblePlugins.js";

const plugins = [
  { id: "shaker", name: "Shaker" },
  { id: "loopkey", name: "LoopKey", experimental: true },
];

test("isExperimentalEnabled accepts flag-style query values", () => {
  assert.equal(isExperimentalEnabled(new URLSearchParams("experimental")), true);
  assert.equal(isExperimentalEnabled(new URLSearchParams("experimental=1")), true);
  assert.equal(isExperimentalEnabled(new URLSearchParams("experimental=false")), false);
  assert.equal(isExperimentalEnabled(new URLSearchParams("plugin=shaker")), false);
});

test("visiblePlugins hides experimental entries by default", () => {
  const params = new URLSearchParams("plugin=shaker");
  assert.deepEqual(visiblePlugins(plugins, params).map((plugin) => plugin.id), ["shaker"]);
});

test("visiblePlugins includes experimental entries when enabled", () => {
  const params = new URLSearchParams("experimental");
  assert.deepEqual(
    visiblePlugins(plugins, params).map((plugin) => plugin.id),
    ["shaker", "loopkey"],
  );
});

test("resolveInitialPlugin honors direct plugin links", () => {
  const params = new URLSearchParams("plugin=loopkey");
  assert.equal(resolveInitialPlugin(plugins, params)?.id, "loopkey");
});

test("resolveInitialPlugin falls back to first visible plugin", () => {
  const params = new URLSearchParams("");
  assert.equal(resolveInitialPlugin(plugins, params)?.id, "shaker");
});

test("findPlugin returns matching entry", () => {
  assert.equal(findPlugin(plugins, "loopkey")?.name, "LoopKey");
  assert.equal(findPlugin(plugins, "missing"), null);
});
