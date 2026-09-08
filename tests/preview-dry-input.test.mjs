import assert from "node:assert/strict";
import test from "node:test";
import {
  defaultDrySourceId,
  DRY_SOURCES,
  isDrySourceId,
} from "../website/src/preview/drySources.js";
import { usesDryInput, usesKickDemo } from "../website/src/preview/layout.js";

test("TechnoRumble and PumpDuck default to the kick source", () => {
  const techno = { id: "technorumble", type: "fx" };
  const pump = { id: "pumpduck", type: "fx" };

  assert.equal(usesKickDemo(techno), true);
  assert.equal(usesKickDemo(pump), true);
  assert.equal(usesDryInput(techno, { target: "nts-3_kaoss" }), true);
  assert.equal(usesDryInput(pump, { target: "nts-1_mkii" }), true);
  assert.equal(defaultDrySourceId(techno), "kick");
  assert.equal(defaultDrySourceId(pump), "kick");
});

test("other insert FX use selectable dry input with a house loop default", () => {
  const plugin = { id: "specwarp", type: "fx" };

  assert.equal(usesKickDemo(plugin), false);
  assert.equal(usesDryInput(plugin, { target: "nts-3_kaoss" }), true);
  assert.equal(usesDryInput(plugin, { target: "nts-1_mkii" }), false);
  assert.equal(defaultDrySourceId(plugin), "house");
});

test("self-contained NTS-3 instruments do not use dry input", () => {
  assert.equal(usesDryInput({ id: "airfm", type: "fm" }, { target: "nts-3_kaoss" }), false);
  assert.equal(usesDryInput({ id: "autrance", type: "synth" }, { target: "nts-3_kaoss" }), false);
});

test("dry source catalog covers club loops and classic test tones", () => {
  const sourceIds = DRY_SOURCES.map((source) => source.id);

  assert.deepEqual(sourceIds, [
    "house",
    "techno",
    "garage",
    "acid",
    "kick",
    "breakbeat",
    "reese",
    "stab",
    "sawtooth",
    "square",
    "sine",
    "triangle",
    "noise",
  ]);
  assert.equal(isDrySourceId("house"), true);
  assert.equal(isDrySourceId("missing"), false);
});
