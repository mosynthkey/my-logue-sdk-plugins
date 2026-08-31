// Emscripten AUDIO_WORKLET glue is a classic per-page program, not a library.
// Vue hosts it here so plugin switches can discard the whole document.
(() => {
  const AHR_ENVELOPE_TIME = 0.1;
  const PREVIEW_TIMEOUT_MS = 20000;

  let audioContext = null;
  let wasmProcessor = null;
  let envelope = null;
  let dryNodes = [];
  let layout = "keyboard";
  let usesDryInput = false;
  let targetName = "";
  let runtimeReady = false;
  let mainStarted = false;
  let runtimeWaiter = null;
  let audioWaiter = null;
  let audioTimeout = 0;
  let activeScript = null;
  let originalAddModule = null;

  function log(kind, message, detail) {
    try {
      if (typeof window.parent.__previewRuntimeLog === "function") {
        window.parent.__previewRuntimeLog(kind, message, detail);
        return;
      }
    } catch {
      // Parent may already be gone during teardown.
    }
    if (kind === "error") {
      console.error(message, detail ?? "");
    } else if (kind === "warn") {
      console.warn(message, detail ?? "");
    } else {
      console.log(message, detail ?? "");
    }
  }

  function wasmJsUrl(wasmHref) {
    return wasmHref.replace(/\.html(?:\?.*)?$/, ".js");
  }

  function wasmBaseUrl(wasmHref) {
    const jsUrl = wasmJsUrl(wasmHref);
    return jsUrl.slice(0, jsUrl.lastIndexOf("/") + 1);
  }

  function isEmscriptenControlFlow(error) {
    return error === "unwind" || error?.message === "unwind";
  }

  function stopDryNodes() {
    for (const node of dryNodes) {
      try {
        if (typeof node.stop === "function") {
          node.stop();
        }
        node.disconnect();
      } catch {
        // Ignore teardown errors.
      }
    }
    dryNodes = [];
  }

  function connectProcessor(context, processor) {
    stopDryNodes();

    if (usesDryInput) {
      const dryGain = context.createGain();
      dryGain.gain.value = 0.2;
      const oscillator = context.createOscillator();
      oscillator.frequency.value = 220;
      oscillator.type = "sawtooth";
      oscillator.connect(dryGain).connect(processor);
      oscillator.start();
      dryNodes.push(oscillator, dryGain);
      return;
    }

    if (targetName === "nts-3_kaoss") {
      const silentGain = context.createGain();
      silentGain.gain.value = 0;
      const silentSource = context.createConstantSource();
      silentSource.offset.value = 0;
      silentSource.connect(silentGain).connect(processor);
      silentSource.start();
      dryNodes.push(silentSource, silentGain);
    }
  }

  function installWorkletModuleBase(baseUrl) {
    const AudioWorkletCtor = window.AudioWorklet;
    if (!AudioWorkletCtor) {
      return;
    }
    const audioWorkletPrototype = AudioWorkletCtor.prototype;
    if (audioWorkletPrototype.__previewModuleBaseUrl === baseUrl) {
      return;
    }
    if (!originalAddModule) {
      originalAddModule = audioWorkletPrototype.addModule;
    }
    audioWorkletPrototype.addModule = function previewAddModule(moduleURL, options) {
      if (
        typeof moduleURL === "string"
        && !moduleURL.includes("/")
        && !/^(?:[a-z]+:|blob:|data:)/i.test(moduleURL)
      ) {
        moduleURL = new URL(moduleURL, baseUrl).href;
      }
      log("info", `AudioWorklet.addModule ${moduleURL}`);
      return originalAddModule.call(this, moduleURL, options).catch((error) => {
        log("error", "AudioWorklet.addModule failed", error);
        throw error;
      });
    };
    audioWorkletPrototype.__previewModuleBaseUrl = baseUrl;
  }

  function loadWasmScript(url) {
    log("info", `Loading wasm script ${url}`);
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = url;
      script.async = true;
      script.onload = () => {
        log("info", "Wasm script loaded");
        resolve(script);
      };
      script.onerror = () => {
        const error = new Error(`Failed to load ${url}`);
        log("error", error.message);
        reject(error);
      };
      document.body.append(script);
      activeScript = script;
    });
  }

  function onAudioReady(context, processor) {
    window.clearTimeout(audioTimeout);
    audioContext = context;
    wasmProcessor = processor;
    const volume = context.createGain();
    volume.gain.value = 0.35;

    if (layout === "keyboard") {
      envelope = context.createGain();
      envelope.gain.value = 0;
      processor.connect(envelope).connect(volume);
    } else {
      connectProcessor(context, processor);
      processor.connect(volume);
    }

    volume.connect(context.destination);
    log("info", "setupWebAudioAndUI called", { state: context.state });
    audioWaiter?.resolve();
    audioWaiter = null;
  }

  function createAudioWaiter() {
    let resolve;
    let reject;
    const promise = new Promise((res, rej) => {
      resolve = res;
      reject = rej;
    });
    audioTimeout = window.setTimeout(() => {
      reject(new Error("Preview timed out waiting for AudioWorklet."));
    }, PREVIEW_TIMEOUT_MS);
    audioWaiter = { promise, resolve, reject };
    return promise;
  }

  function waitForRuntime() {
    if (runtimeReady) {
      return Promise.resolve();
    }
    return new Promise((resolve) => {
      runtimeWaiter = resolve;
    });
  }

  function readKnobs() {
    const moduleRef = window.Module;
    const parameters = moduleRef.getValidParameters();
    const knobs = [];
    for (let paramIndex = 0; paramIndex < parameters.size(); paramIndex += 1) {
      const param = parameters.get(paramIndex);
      knobs.push({
        name: param.name,
        min: param.min,
        max: param.max,
        value: param.init,
        index: paramIndex,
        placeholder: false,
        valueLabel: formatKnobValue(paramIndex, param.init),
      });
    }
    return knobs;
  }

  function formatKnobValue(index, value) {
    const moduleRef = window.Module;
    if (moduleRef?.getParameterValueString) {
      return moduleRef.getParameterValueString(index, value);
    }
    return String(Math.round(value));
  }

  function readMappings(knobCount) {
    const moduleRef = window.Module;
    const mappings = moduleRef.getDefaultMapping();
    const entries = [];
    for (let paramIndex = 0; paramIndex < knobCount; paramIndex += 1) {
      const mapping = mappings.get(paramIndex);
      if (!mapping) {
        continue;
      }
      entries.push({
        paramIndex,
        assign: mapping.assign,
        curve: mapping.curve,
        unipolar: mapping.unipolar,
        init: mapping.init,
      });
    }
    return {
      entries,
      paramAssign: {
        X: moduleRef.ParamAssign.X,
        Y: moduleRef.ParamAssign.Y,
      },
    };
  }

  function setParam(index, value) {
    const audioParameter = wasmProcessor?.parameters.get(`${index}`);
    if (audioParameter) {
      audioParameter.value = value;
    }
  }

  function resumeAudio() {
    if (!audioContext) {
      return false;
    }
    if (audioContext.state !== "running") {
      audioContext.resume();
    }
    return audioContext.state !== "suspended";
  }

  function setGate(open) {
    if (!envelope || !audioContext) {
      return;
    }
    envelope.gain.cancelAndHoldAtTime(audioContext.currentTime);
    envelope.gain.linearRampToValueAtTime(
      open ? 1.0 : 0.0,
      audioContext.currentTime + AHR_ENVELOPE_TIME,
    );
  }

  window.__previewHost = {
    async configureAndLoad({ wasmHref, layoutName, dryInput, target, deferMain }) {
      layout = layoutName;
      usesDryInput = Boolean(dryInput);
      targetName = target || "";
      runtimeReady = false;
      mainStarted = false;
      const baseUrl = wasmBaseUrl(wasmHref);
      const jsUrl = `${wasmJsUrl(wasmHref)}?v=${Date.now()}`;
      const audioReady = createAudioWaiter();

      const moduleConfig = {
        locateFile: (path) => new URL(path, baseUrl).href,
        mainScriptUrlOrBlob: jsUrl,
        noInitialRun: deferMain,
        onAudioReady,
        printErr: (message) => log("error", `[wasm] ${message}`),
        onRuntimeInitialized: () => {
          runtimeReady = true;
          log("info", "Wasm runtime initialized");
          runtimeWaiter?.();
          runtimeWaiter = null;
        },
      };
      window.Module = moduleConfig;
      window.setupWebAudioAndUI = onAudioReady;
      installWorkletModuleBase(baseUrl);
      log("info", "Configured wasm module", {
        wasm: wasmHref,
        crossOriginIsolated: window.crossOriginIsolated,
        hasAudioContext: typeof AudioContext !== "undefined" || typeof webkitAudioContext !== "undefined",
        deferMain,
      });

      try {
        await loadWasmScript(jsUrl);
        await waitForRuntime();
      } catch (error) {
        window.clearTimeout(audioTimeout);
        audioWaiter?.reject(error);
        throw error;
      }
      if (!deferMain) {
        mainStarted = true;
      }
      return { audioReady };
    },

    startMain() {
      const moduleRef = window.Module;
      if (!moduleRef?.calledRun || typeof moduleRef._main !== "function") {
        log("info", "Wasm runtime not ready for main()");
        return false;
      }
      if (mainStarted || moduleRef.__previewMainStarted) {
        return true;
      }

      moduleRef.__previewMainStarted = true;
      try {
        log("info", "Calling Module._main(0, 0) from user gesture");
        moduleRef._main(0, 0);
        mainStarted = true;
        return true;
      } catch (error) {
        if (isEmscriptenControlFlow(error)) {
          mainStarted = true;
          log("info", "Module._main resumed async audio init (unwind)");
          return true;
        }
        moduleRef.__previewMainStarted = false;
        log("error", "Module._main(0, 0) failed", error?.message || String(error));
        return false;
      }
    },

    readKnobs,
    formatKnobValue,
    readMappings,
    setParam,
    applyCurve(normalized, curve, unipolar) {
      return window.Module.applyCurveToParameter0to1(normalized, curve, unipolar);
    },
    resumeAudio,
    suspendAudio() {
      audioContext?.suspend();
    },
    audioState() {
      return audioContext?.state || "closed";
    },
    setOscPitch(frequency) {
      window.Module.setOscPitch(frequency);
    },
    noteOn(note, velocity) {
      window.Module.noteOn(note, velocity);
    },
    noteOff(note) {
      window.Module.noteOff(note);
    },
    setGate,
    touchBegan(xNormalized, yNormalized) {
      resumeAudio();
      window.Module.touchEvent(window.Module.TouchEvent.Began, xNormalized, yNormalized);
    },
    touchMoved(xNormalized, yNormalized) {
      window.Module.touchEvent(window.Module.TouchEvent.Moved, xNormalized, yNormalized);
    },
    touchEnded(xNormalized, yNormalized) {
      window.Module.touchEvent(window.Module.TouchEvent.Ended, xNormalized, yNormalized);
    },
  };
})();
