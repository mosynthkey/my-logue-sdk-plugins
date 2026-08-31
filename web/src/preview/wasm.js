import { PREVIEW_TIMEOUT_MS } from "./constants.js";
import { previewDebugLog } from "../composables/usePreviewDebugLog.js";
import { onWasmRuntimeInitialized } from "./wasm-runtime.js";

let activeScript = null;

export function wasmJsUrl(wasmHref) {
  return wasmHref.replace(/\.html(?:\?.*)?$/, ".js");
}

export function wasmBaseUrl(wasmHref) {
  const jsUrl = wasmJsUrl(wasmHref);
  return jsUrl.slice(0, jsUrl.lastIndexOf("/") + 1);
}

export function installWorkletModuleBase(baseUrl) {
  const resolvedBase = new URL(baseUrl, location.href).href;
  const audioWorkletPrototype = AudioWorklet.prototype;
  if (audioWorkletPrototype.__previewModuleBaseUrl === resolvedBase) {
    return;
  }

  const originalAddModule = audioWorkletPrototype.__previewOriginalAddModule
    || audioWorkletPrototype.addModule;

  audioWorkletPrototype.addModule = function previewAddModule(moduleURL, options) {
    if (
      typeof moduleURL === "string"
      && !moduleURL.includes("/")
      && !/^(?:[a-z]+:|blob:|data:)/i.test(moduleURL)
    ) {
      moduleURL = new URL(moduleURL, resolvedBase).href;
    }
    previewDebugLog("info", `AudioWorklet.addModule ${moduleURL}`);
    return originalAddModule.call(this, moduleURL, options).catch((error) => {
      previewDebugLog("error", "AudioWorklet.addModule failed", error);
      throw error;
    });
  };

  audioWorkletPrototype.__previewOriginalAddModule = originalAddModule;
  audioWorkletPrototype.__previewModuleBaseUrl = resolvedBase;
}

export function loadWasmScript(url) {
  previewDebugLog("info", `Loading wasm script ${url}`);
  return new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = url;
    script.async = true;
    script.onload = () => {
      previewDebugLog("info", "Wasm script loaded");
      resolve(script);
    };
    script.onerror = () => {
      const error = new Error(`Failed to load ${url}`);
      previewDebugLog("error", error.message);
      reject(error);
    };
    document.body.append(script);
    activeScript = script;
  });
}

export function removeActiveWasmScript() {
  if (activeScript) {
    activeScript.remove();
    activeScript = null;
  }
}

export function configureWasmModule(build) {
  const baseUrl = wasmBaseUrl(build.wasm);
  const jsUrl = `${wasmJsUrl(build.wasm)}?v=${Date.now()}`;
  const moduleConfig = {
    locateFile: (path) => baseUrl + path,
    mainScriptUrlOrBlob: jsUrl,
    noInitialRun: true,
    printErr: (message) => previewDebugLog("error", `[wasm] ${message}`),
    onRuntimeInitialized: () => {
      onWasmRuntimeInitialized();
    },
  };
  window.Module = moduleConfig;
  globalThis.Module = moduleConfig;
  installWorkletModuleBase(baseUrl);
  previewDebugLog("info", "Configured wasm module", {
    wasm: build.wasm,
    crossOriginIsolated: window.crossOriginIsolated,
    hasAudioContext: typeof AudioContext !== "undefined" || typeof webkitAudioContext !== "undefined",
  });
  return { baseUrl, jsUrl };
}

export function waitForWasmReady(generation, previewGenerationRef) {
  return new Promise((resolve, reject) => {
    const timeout = window.setTimeout(() => {
      const hint = "Add ?previewDebug=1 to the URL for an on-screen log. "
        + "On Mac, use Safari > Develop > [device] to inspect.";
      reject(new Error(`Preview timed out. ${hint}`));
    }, PREVIEW_TIMEOUT_MS);

    const handler = (context, wasmProcessor) => {
      if (generation !== previewGenerationRef.value) {
        return;
      }
      window.clearTimeout(timeout);
      previewDebugLog("info", "setupWebAudioAndUI called", {
        state: context?.state,
      });
      resolve({ context, wasmProcessor });
    };

    window.setupWebAudioAndUI = handler;
    globalThis.setupWebAudioAndUI = handler;
  });
}

export function clearWasmGlobals() {
  removeActiveWasmScript();
  delete window.setupWebAudioAndUI;
  delete globalThis.setupWebAudioAndUI;
  delete window.Module;
  delete globalThis.Module;
}
