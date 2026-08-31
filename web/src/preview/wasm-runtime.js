import { previewDebugLog } from "../composables/usePreviewDebugLog.js";
import { needsGestureForWasmStart } from "./gesture.js";

let wasmRuntimeReady = false;
let wasmMainStarted = false;
let runtimeWaiter = null;

export function isWasmRuntimeReady() {
  return wasmRuntimeReady;
}

export function isWasmMainStarted() {
  return wasmMainStarted;
}

export function resetWasmRuntimeState() {
  wasmRuntimeReady = false;
  wasmMainStarted = false;
  runtimeWaiter = null;
}

export function waitForWasmRuntime() {
  if (wasmRuntimeReady) {
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    runtimeWaiter = resolve;
  });
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
    previewDebugLog("info", "Calling Module._main(0, 0) from user gesture");
    moduleRef.__previewMainStarted = true;
    moduleRef._main(0, 0);
    wasmMainStarted = true;
    return true;
  } catch (error) {
    moduleRef.__previewMainStarted = false;
    wasmMainStarted = false;
    const detail = error?.message || String(error);
    previewDebugLog("error", "Module._main(0, 0) failed", detail);
    if (!window.crossOriginIsolated) {
      previewDebugLog(
        "error",
        "crossOriginIsolated is false — AudioWorklet preview may not work on this browser",
      );
    }
    return false;
  }
}

export async function ensureWasmStarted() {
  if (!needsGestureForWasmStart()) {
    return true;
  }

  await waitForWasmRuntime();
  return wasmMainStarted;
}

export function onWasmRuntimeInitialized() {
  wasmRuntimeReady = true;
  previewDebugLog("info", "Wasm runtime initialized");
  runtimeWaiter?.();
  runtimeWaiter = null;
}

export { needsGestureForWasmStart };
