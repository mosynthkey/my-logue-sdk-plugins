import { previewDebugLog } from "../composables/usePreviewDebugLog.js";
import { needsGestureForWasmStart } from "./gesture.js";
import { getModule } from "./wasm-host.js";

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

function isEmscriptenControlFlow(error) {
  return error === "unwind" || error?.message === "unwind";
}

export function startWasmMainInGesture() {
  const moduleRef = getModule();
  if (!moduleRef?.calledRun || typeof moduleRef._main !== "function") {
    previewDebugLog("info", "Wasm runtime not ready for main()");
    return false;
  }
  if (wasmMainStarted || moduleRef.__previewMainStarted) {
    return true;
  }

  moduleRef.__previewMainStarted = true;

  try {
    previewDebugLog("info", "Calling Module._main(0, 0) from user gesture");
    moduleRef._main(0, 0);
    wasmMainStarted = true;
    return true;
  } catch (error) {
    if (isEmscriptenControlFlow(error)) {
      wasmMainStarted = true;
      previewDebugLog("info", "Module._main resumed async audio init (unwind)");
      return true;
    }

    moduleRef.__previewMainStarted = false;
    wasmMainStarted = false;
    const detail = error?.message || String(error);
    previewDebugLog("error", "Module._main(0, 0) failed", detail);
    if (!window.crossOriginIsolated) {
      previewDebugLog(
        "warn",
        "crossOriginIsolated is false — preview may still work if AudioWorklet loads",
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
