import assert from "node:assert/strict";
import test from "node:test";
import {
  depthPadPointFromEvent,
  knobValueFromDrag,
} from "../website/src/preview/geometry.js";

test("a fixed drag distance spans the full value range", () => {
  assert.equal(knobValueFromDrag(0, 160, 0, 1), 1);
  assert.equal(knobValueFromDrag(30, 160, 30, 240), 240);
  assert.equal(knobValueFromDrag(0, 160, 0, 1023), 1023);
});

test("drag values are clamped at both ends", () => {
  assert.equal(knobValueFromDrag(50, 500, 0, 100), 100);
  assert.equal(knobValueFromDrag(50, -500, 0, 100), 0);
});

test("equal bounds produce the only valid value", () => {
  assert.equal(knobValueFromDrag(20, 160, 7, 7), 7);
});

test("depth pad maps bottom to min and top to max", () => {
  const canvas = {
    getBoundingClientRect: () => ({ left: 0, top: 10, width: 40, height: 200 }),
  };

  assert.equal(depthPadPointFromEvent(canvas, { clientY: 210 }).depthNormalized, 0);
  assert.equal(depthPadPointFromEvent(canvas, { clientY: 10 }).depthNormalized, 1);
});
