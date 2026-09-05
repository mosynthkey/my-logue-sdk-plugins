import assert from "node:assert/strict";
import test from "node:test";
import { usesDryInput, usesKickDemo } from "../website/src/preview/layout.js";

test("TechnoRumble preview uses kick demo instead of generic dry input", () => {
  const plugin = { id: "technorumble", type: "fx" };

  assert.equal(usesKickDemo(plugin), true);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), false);
  assert.equal(usesDryInput(plugin, { target: "nts-1_mkii" }), false);
});

test("PumpDuck preview uses kick demo so the follower has a transient", () => {
  const plugin = { id: "pumpduck", type: "fx" };

  assert.equal(usesKickDemo(plugin), true);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), false);
});

test("other insert FX still use the sawtooth dry input on NTS-3", () => {
  const plugin = { id: "specwarp", type: "fx" };

  assert.equal(usesKickDemo(plugin), false);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), true);
});
