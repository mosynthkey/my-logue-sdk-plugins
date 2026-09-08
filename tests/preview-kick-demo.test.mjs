import assert from "node:assert/strict";
import test from "node:test";
import { defaultDrySourceId } from "../website/src/preview/drySources.js";
import { usesDryInput, usesKickDemo } from "../website/src/preview/layout.js";

test("TechnoRumble preview prefers the kick source instead of a generic tone", () => {
  const plugin = { id: "technorumble", type: "fx" };

  assert.equal(usesKickDemo(plugin), true);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), true);
  assert.equal(usesDryInput(plugin, { target: "nts-1_mkii" }), true);
  assert.equal(defaultDrySourceId(plugin), "kick");
});

test("PumpDuck preview prefers the kick source so the follower has a transient", () => {
  const plugin = { id: "pumpduck", type: "fx" };

  assert.equal(usesKickDemo(plugin), true);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), true);
  assert.equal(defaultDrySourceId(plugin), "kick");
});

test("other insert FX still use selectable dry input on NTS-3", () => {
  const plugin = { id: "specwarp", type: "fx" };

  assert.equal(usesKickDemo(plugin), false);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), true);
  assert.equal(defaultDrySourceId(plugin), "house");
});
