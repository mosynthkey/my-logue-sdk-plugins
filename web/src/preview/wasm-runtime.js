import { whenAudioUnlocked } from "../composables/useAudioSession.js";
import { previewDebugLog } from "../composables/usePreviewDebugLog.js";

let wasmRuntimeReady = false;
let wasmMainStarted = false;

export function isWasmRuntimeReady() {
  return wasmRuntimeReady;
}

export function isWasmMainStarted() {
  return wasmMainStarted;
}

export function resetWasmRuntimeState() {
  wasmRuntimeReady = false;
  wasmMainStarted = false;
}

export function needsGestureForWasmStart() {
  const mobileLayout = window.matchMedia("(max-width: 820px), (pointer: coarse)").matches;
  return mobileLayout || !window.crossOriginIsolated;
}

export function startWasmMainInGesture() {
  const moduleRef = window.Module;
  if (!moduleRef?.calledRun || typeof moduleRef._main !== "function") {
    previewDebugLog("info", "Wasm runtime not ready for main()");
    return false;
  }
  if (wasmMainStarted || moduleRef.__previewMainStarted) {
    return true;
  }

  try {
    moduleRef.__previewMainStarted = true;
    wasmMainStarted = true;
    previewDebugLog("info", "Calling Module._main() from user gesture");
    moduleRef._main();
    return true;
  } catch (error) {
    wasmMainStarted = false;
    moduleRef.__previewMainStarted = false;
    previewDebugLog("error", "Module._main() failed", error);
    return false;
  }
}

export async function ensureWasmStarted() {
  await whenAudioUnlocked();

  if (startWasmMainInGesture()) {
    return true;
  }

  if (!wasmRuntimeReady) {
    previewDebugLog("info", "Waiting for wasm runtime");
    return false;
  }

  if (needsGestureForWasmStart()) {
    previewDebugLog("warn", "Wasm ready but needs another tap to start main()");
    return false;
  }

  return startWasmMainInGesture();
}

export function onWasmRuntimeInitialized() {
  wasmRuntimeReady = true;
  previewDebugLog("info", "Wasm runtime initialized");
}
