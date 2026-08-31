import { PREVIEW_TIMEOUT_MS } from "./constants.js";

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
    return originalAddModule.call(this, moduleURL, options);
  };

  audioWorkletPrototype.__previewOriginalAddModule = originalAddModule;
  audioWorkletPrototype.__previewModuleBaseUrl = resolvedBase;
}

export function loadWasmScript(url) {
  return new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = url;
    script.async = true;
    script.onload = () => resolve(script);
    script.onerror = () => reject(new Error(`Failed to load ${url}`));
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
    printErr: (message) => console.error("[preview wasm]", message),
  };
  window.Module = moduleConfig;
  globalThis.Module = moduleConfig;
  installWorkletModuleBase(baseUrl);
  return { baseUrl, jsUrl };
}

export function waitForWasmReady(generation, previewGenerationRef) {
  return new Promise((resolve, reject) => {
    const timeout = window.setTimeout(() => {
      reject(new Error("Preview timed out. Tap to start preview, then try again."));
    }, PREVIEW_TIMEOUT_MS);

    const handler = (context, wasmProcessor) => {
      if (generation !== previewGenerationRef.value) {
        return;
      }
      window.clearTimeout(timeout);
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
