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

export function knobValueFromDrag(startValue, deltaY, min, max, dragRangePx = 160) {
  if (dragRangePx <= 0 || max === min) {
    return min;
  }
  const nextValue = startValue + (deltaY / dragRangePx) * (max - min);
  return Math.min(max, Math.max(min, nextValue));
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

function accentColor() {
  return getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() || "#b6b2a1";
}

export function drawXypadGrid(ctx, width, height) {
  ctx.fillStyle = "#0d0d0d";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "#222222";
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
  ctx.strokeStyle = "#333333";
  ctx.beginPath();
  ctx.moveTo(width / 2, 0);
  ctx.lineTo(width / 2, height);
  ctx.moveTo(0, height / 2);
  ctx.lineTo(width, height / 2);
  ctx.stroke();
}

export function drawXypadMarker(ctx, x, y) {
  const color = accentColor();
  ctx.beginPath();
  ctx.arc(x, y, 16, 0, Math.PI * 2);
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(x, y, 3.5, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.fill();
}

export function paintXypad(canvas, marker) {
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 2 || rect.height < 2) {
    return;
  }
  const cssWidth = rect.width;
  const cssHeight = rect.height;
  const dpr = window.devicePixelRatio || 1;
  const pixelWidth = Math.max(1, Math.round(cssWidth * dpr));
  const pixelHeight = Math.max(1, Math.round(cssHeight * dpr));
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawXypadGrid(ctx, cssWidth, cssHeight);
  if (marker) {
    drawXypadMarker(ctx, marker.x, marker.y);
  }
}

export function xyPadPointFromEvent(canvas, event) {
  const rect = canvas.getBoundingClientRect();
  const x = Math.min(Math.max(event.clientX - rect.left, 0), rect.width);
  const y = Math.min(Math.max(event.clientY - rect.top, 0), rect.height);
  return {
    x,
    y,
    xNormalized: rect.width ? x / rect.width : 0,
    yNormalized: rect.height ? y / rect.height : 0,
  };
}

export function drawDepthPadBackground(ctx, width, height) {
  ctx.fillStyle = "#0d0d0d";
  ctx.fillRect(0, 0, width, height);
}

export function drawDepthPadMarker(ctx, width, y) {
  const color = accentColor();
  ctx.beginPath();
  ctx.moveTo(4, y);
  ctx.lineTo(width - 4, y);
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(width / 2, y, 3.5, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.fill();
}

export function paintDepthPad(canvas, markerY) {
  const rect = canvas.getBoundingClientRect();
  if (rect.width < 2 || rect.height < 2) {
    return;
  }
  const cssWidth = rect.width;
  const cssHeight = rect.height;
  const dpr = window.devicePixelRatio || 1;
  const pixelWidth = Math.max(1, Math.round(cssWidth * dpr));
  const pixelHeight = Math.max(1, Math.round(cssHeight * dpr));
  if (canvas.width !== pixelWidth || canvas.height !== pixelHeight) {
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawDepthPadBackground(ctx, cssWidth, cssHeight);
  if (Number.isFinite(markerY)) {
    drawDepthPadMarker(ctx, cssWidth, markerY);
  }
}

export function depthPadPointFromEvent(canvas, event) {
  const rect = canvas.getBoundingClientRect();
  const y = Math.min(Math.max(event.clientY - rect.top, 0), rect.height);
  return {
    y,
    depthNormalized: rect.height ? 1 - y / rect.height : 0,
  };
}
