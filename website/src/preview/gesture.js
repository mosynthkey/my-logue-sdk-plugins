export function needsGestureForWasmStart() {
  const mobileLayout = window.matchMedia("(max-width: 820px), (pointer: coarse)").matches;
  return mobileLayout || !window.crossOriginIsolated;
}
