import { KNOB_ARC_START, KNOB_ARC_SWEEP } from "./constants.js";

export function normalizeKnobValue(value, min, max) {
  if (max === min) {
    return 0;
  }
  return (value - min) / (max - min);
}

export function knobAngle(normalized) {
  const clamped = Math.min(Math.max(normalized, 0), 1);
  return KNOB_ARC_START + clamped * KNOB_ARC_SWEEP;
}

function polarPoint(center, radius, angleDeg) {
  const angleRad = ((angleDeg - 90) * Math.PI) / 180;
  return {
    x: center + radius * Math.cos(angleRad),
    y: center + radius * Math.sin(angleRad),
  };
}

export function arcPath(center, radius, startDeg, endDeg) {
  const start = polarPoint(center, radius, startDeg);
  const end = polarPoint(center, radius, endDeg);
  const largeArc = endDeg - startDeg > 180 ? 1 : 0;
  return `M ${start.x.toFixed(2)} ${start.y.toFixed(2)} A ${radius} ${radius} 0 ${largeArc} 1 ${end.x.toFixed(2)} ${end.y.toFixed(2)}`;
}

export function pointerPath(center, radius, angleDeg) {
  const inner = polarPoint(center, radius - 11, angleDeg);
  const outer = polarPoint(center, radius - 3, angleDeg);
  return `M ${inner.x.toFixed(2)} ${inner.y.toFixed(2)} L ${outer.x.toFixed(2)} ${outer.y.toFixed(2)}`;
}

export function drawXypadGrid(ctx, width, height) {
  ctx.fillStyle = "#0d0d0d";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "#1e1e1e";
  ctx.lineWidth = 1;
  for (let gridIndex = 1; gridIndex < 8; gridIndex += 1) {
    const x = (width / 8) * gridIndex;
    const y = (height / 8) * gridIndex;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
  }
  ctx.strokeStyle = "#2a2a2a";
  ctx.beginPath();
  ctx.moveTo(width / 2, 0);
  ctx.lineTo(width / 2, height);
  ctx.moveTo(0, height / 2);
  ctx.lineTo(width, height / 2);
  ctx.stroke();
}
