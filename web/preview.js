const AHREnvelopeTime = 0.1;
const KNOB_ARC_START = 225;
const KNOB_ARC_SWEEP = 270;
const KNOB_CENTER = 24;
const KNOB_RADIUS = 19;
const PREVIEW_TIMEOUT_MS = 20000;

const SELF_CONTAINED_TYPES = new Set(["osc", "fm", "synth", "drum", "loopkey"]);

let knobIdCounter = 0;
let activeScript = null;
let audioContext = null;
let keyboard = null;
let frequencyStack = [];
let latchEnabled = false;
let xyPadHold = false;
let lastXYPadEvent = { clientX: 0, clientY: 0 };
let previewGeneration = 0;
let dryInputNodes = [];
let activePreviewIframe = null;
let parentMountToken = 0;

function approximatelyEqual(a, b, epsilon = 1e-4) {
  return Math.abs(a - b) < epsilon;
}

function wasmJsUrl(wasmHref) {
  return wasmHref.replace(/\.html(?:\?.*)?$/, ".js");
}

function wasmBaseUrl(wasmHref) {
  const jsUrl = wasmJsUrl(wasmHref);
  return jsUrl.slice(0, jsUrl.lastIndexOf("/") + 1);
}

function installWorkletModuleBase(baseUrl) {
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

function previewLayout(build) {
  if (build.target === "nts-3_kaoss") {
    return "xypad";
  }
  return "keyboard";
}

function usesDryInput(plugin, build) {
  return build.target === "nts-3_kaoss" && !SELF_CONTAINED_TYPES.has(plugin.type);
}

function loadScript(url) {
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

function removeActiveScript() {
  if (activeScript) {
    activeScript.remove();
    activeScript = null;
  }
}

function stopDryInputNodes() {
  for (const node of dryInputNodes) {
    try {
      if (typeof node.stop === "function") {
        node.stop();
      }
      node.disconnect();
    } catch {
      // Ignore teardown errors.
    }
  }
  dryInputNodes = [];
}

function ensureAudioRunning(toggleButton) {
  if (!audioContext) {
    return;
  }
  if (audioContext.state !== "running") {
    audioContext.resume();
  }
  if (toggleButton) {
    toggleButton.textContent = "Suspend audio";
    toggleButton.classList.add("is-on");
  }
}

function createToolbar() {
  const toolbar = document.createElement("div");
  toolbar.className = "preview-toolbar";

  const audioButton = document.createElement("button");
  audioButton.type = "button";
  audioButton.className = "preview-chip";
  audioButton.textContent = "Start audio";
  audioButton.addEventListener("click", () => {
    if (!audioContext) {
      return;
    }
    if (audioContext.state !== "running") {
      ensureAudioRunning(audioButton);
    } else {
      audioContext.suspend();
      audioButton.textContent = "Start audio";
      audioButton.classList.remove("is-on");
    }
  });

  toolbar.append(audioButton);
  return { toolbar, audioButton };
}

function normalizeKnobValue(value, min, max) {
  if (max === min) {
    return 0;
  }
  return (value - min) / (max - min);
}

function knobAngle(normalized) {
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

function arcPath(center, radius, startDeg, endDeg) {
  const start = polarPoint(center, radius, startDeg);
  const end = polarPoint(center, radius, endDeg);
  const largeArc = endDeg - startDeg > 180 ? 1 : 0;
  return `M ${start.x.toFixed(2)} ${start.y.toFixed(2)} A ${radius} ${radius} 0 ${largeArc} 1 ${end.x.toFixed(2)} ${end.y.toFixed(2)}`;
}

function pointerPath(center, radius, angleDeg) {
  const inner = polarPoint(center, radius - 11, angleDeg);
  const outer = polarPoint(center, radius - 3, angleDeg);
  return `M ${inner.x.toFixed(2)} ${inner.y.toFixed(2)} L ${outer.x.toFixed(2)} ${outer.y.toFixed(2)}`;
}

function drawXypadGrid(ctx, width, height) {
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

function createKnob(param, index, wasmProcessor) {
  const knobId = `knob-${knobIdCounter += 1}`;
  const wrap = document.createElement("div");
  wrap.className = "knob";

  const dial = document.createElement("div");
  dial.className = "knob__dial";

  const svgNS = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(svgNS, "svg");
  svg.setAttribute("class", "knob__svg");
  svg.setAttribute("viewBox", "0 0 48 48");

  const defs = document.createElementNS(svgNS, "defs");
  const gradient = document.createElementNS(svgNS, "linearGradient");
  gradient.setAttribute("id", `${knobId}-cap`);
  gradient.setAttribute("x1", "0");
  gradient.setAttribute("y1", "0");
  gradient.setAttribute("x2", "0");
  gradient.setAttribute("y2", "1");
  const stopTop = document.createElementNS(svgNS, "stop");
  stopTop.setAttribute("offset", "0%");
  stopTop.setAttribute("stop-color", "#3a3a3a");
  const stopBottom = document.createElementNS(svgNS, "stop");
  stopBottom.setAttribute("offset", "100%");
  stopBottom.setAttribute("stop-color", "#222222");
  gradient.append(stopTop, stopBottom);
  defs.append(gradient);
  svg.append(defs);

  const track = document.createElementNS(svgNS, "path");
  track.setAttribute("class", "knob__track");
  track.setAttribute(
    "d",
    arcPath(KNOB_CENTER, KNOB_RADIUS, KNOB_ARC_START, KNOB_ARC_START + KNOB_ARC_SWEEP),
  );

  const valueArc = document.createElementNS(svgNS, "path");
  valueArc.setAttribute("class", "knob__value-arc");

  const cap = document.createElementNS(svgNS, "circle");
  cap.setAttribute("class", "knob__cap");
  cap.setAttribute("cx", String(KNOB_CENTER));
  cap.setAttribute("cy", String(KNOB_CENTER));
  cap.setAttribute("r", "13");
  cap.setAttribute("fill", `url(#${knobId}-cap)`);

  const pointer = document.createElementNS(svgNS, "path");
  pointer.setAttribute("class", "knob__pointer");

  svg.append(track, valueArc, cap, pointer);
  dial.append(svg);

  const label = document.createElement("div");
  label.className = "knob__label";
  label.textContent = param.name;

  const valueEl = document.createElement("div");
  valueEl.className = "knob__value";

  wrap.append(dial, label, valueEl);

  let currentValue = param.init;
  let dragStartY = 0;
  let dragStartValue = 0;

  function updateVisual(normalized) {
    const angle = knobAngle(normalized);
    if (normalized <= 0.001) {
      valueArc.setAttribute("d", "");
    } else {
      valueArc.setAttribute("d", arcPath(KNOB_CENTER, KNOB_RADIUS, KNOB_ARC_START, angle));
    }
    pointer.setAttribute("d", pointerPath(KNOB_CENTER, KNOB_RADIUS, angle));
  }

  function setValue(nextValue, dispatch = true) {
    currentValue = Math.min(Math.max(nextValue, param.min), param.max);
    const normalized = normalizeKnobValue(currentValue, param.min, param.max);
    updateVisual(normalized);
    if (window.Module?.getParameterValueString) {
      valueEl.textContent = Module.getParameterValueString(index, currentValue);
    } else {
      valueEl.textContent = String(Math.round(currentValue));
    }
    if (dispatch && wasmProcessor) {
      const audioParameter = wasmProcessor.parameters.get(`${index}`);
      if (audioParameter) {
        audioParameter.value = currentValue;
      }
    }
  }

  setValue(currentValue, false);

  dial.addEventListener("pointerdown", (event) => {
    dial.setPointerCapture(event.pointerId);
    dragStartY = event.clientY;
    dragStartValue = currentValue;
  });

  dial.addEventListener("pointermove", (event) => {
    if (!dial.hasPointerCapture(event.pointerId)) {
      return;
    }
    const delta = (dragStartY - event.clientY) * 0.4;
    setValue(dragStartValue + delta);
  });

  dial.addEventListener("pointerup", (event) => {
    dial.releasePointerCapture(event.pointerId);
  });

  return { element: wrap, setValue, param, index };
}

function mountPlaceholderKnobs(container, plugin) {
  container.innerHTML = "";
  container.hidden = false;
  for (const param of plugin.params || []) {
    const fakeParam = {
      name: param.name,
      min: 0,
      max: 100,
      init: 50,
    };
    const knob = createKnob(fakeParam, 0, null);
    knob.element.classList.add("knob--placeholder");
    container.append(knob.element);
  }
}

function mountMk2Keyboard(container, context, envelope, audioButton) {
  const keyboardShell = document.createElement("div");
  keyboardShell.className = "preview-keyboard-shell";

  const keyboardWrap = document.createElement("div");
  keyboardWrap.className = "preview-keyboard-wrap preview-keyboard-wrap--mk2";
  const keyboardHost = document.createElement("div");
  keyboardHost.id = "preview-keyboard";
  keyboardWrap.append(keyboardHost);
  keyboardShell.append(keyboardWrap);
  container.append(keyboardShell);

  let currentOctave = 3;
  const keyboardWidth = Math.min(520, container.clientWidth - 32 || 520);
  keyboard = new QwertyHancock({
    id: "preview-keyboard",
    width: keyboardWidth,
    height: 96,
    octaves: 2,
    startNote: "C3",
    whiteNotesColour: "#ddd9ce",
    blackNotesColour: "#141414",
    hoverColour: "#b6b2a1",
    activeColour: "#b6b2a1",
  });
  keyboard.setKeyOctave(currentOctave);

  keyboard.keyDown = (note, frequency) => {
    ensureAudioRunning(audioButton);
    frequencyStack.push(frequency);
    Module.setOscPitch(frequency);
    Module.noteOn(note, 100);
    envelope.gain.cancelAndHoldAtTime(context.currentTime);
    envelope.gain.linearRampToValueAtTime(1.0, context.currentTime + AHREnvelopeTime);
  };

  keyboard.keyUp = (note, frequency) => {
    for (let stackIndex = frequencyStack.length - 1; stackIndex >= 0; stackIndex -= 1) {
      if (approximatelyEqual(frequencyStack[stackIndex], frequency)) {
        frequencyStack.splice(stackIndex, 1);
        break;
      }
    }

    if (frequencyStack.length > 0) {
      Module.setOscPitch(frequencyStack[frequencyStack.length - 1]);
    } else if (!latchEnabled) {
      envelope.gain.cancelAndHoldAtTime(context.currentTime);
      envelope.gain.linearRampToValueAtTime(0.0, context.currentTime + AHREnvelopeTime);
    }

    Module.noteOff(note);
  };

  const onOctaveKeyDown = (event) => {
    if (event.key === "z") {
      currentOctave = Math.max(0, currentOctave - 1);
      keyboard.setKeyOctave(currentOctave);
    } else if (event.key === "x") {
      currentOctave = Math.min(7, currentOctave + 1);
      keyboard.setKeyOctave(currentOctave);
    }
  };

  document.addEventListener("keydown", onOctaveKeyDown);
  container._cleanup = () => {
    document.removeEventListener("keydown", onOctaveKeyDown);
  };
}

function mountOscInstrument(container, context, envelope) {
  container.innerHTML = "";
  const { toolbar, audioButton } = createToolbar();

  const latchButton = document.createElement("button");
  latchButton.type = "button";
  latchButton.className = "preview-chip";
  latchButton.textContent = "Latch Off";
  latchButton.addEventListener("click", () => {
    latchEnabled = !latchEnabled;
    latchButton.textContent = `Latch ${latchEnabled ? "On" : "Off"}`;
    latchButton.classList.toggle("is-on", latchEnabled);
    if (!latchEnabled && frequencyStack.length === 0) {
      envelope.gain.cancelAndHoldAtTime(context.currentTime);
      envelope.gain.linearRampToValueAtTime(0.0, context.currentTime + AHREnvelopeTime);
    }
  });

  toolbar.append(latchButton);
  container.append(toolbar);
  mountMk2Keyboard(container, context, envelope, audioButton);
}

function mountXypadInstrument(container) {
  container.innerHTML = "";
  const { toolbar } = createToolbar();

  const holdButton = document.createElement("button");
  holdButton.type = "button";
  holdButton.className = "preview-chip";
  holdButton.textContent = "Hold Off";

  const padShell = document.createElement("div");
  padShell.className = "preview-xypad-shell";

  const canvas = document.createElement("canvas");
  canvas.className = "preview-xypad";
  canvas.width = 360;
  canvas.height = 240;

  padShell.append(canvas);
  toolbar.append(holdButton);
  container.append(toolbar, padShell);

  const padCtx = canvas.getContext("2d");
  drawXypadGrid(padCtx, canvas.width, canvas.height);

  holdButton.addEventListener("click", () => {
    xyPadHold = !xyPadHold;
    holdButton.textContent = `Hold ${xyPadHold ? "On" : "Off"}`;
    holdButton.classList.toggle("is-on", xyPadHold);
    if (!xyPadHold) {
      updateXYPad(lastXYPadEvent, Module.TouchEvent.Ended);
    }
  });

  function updateXYPad(event, phase) {
    lastXYPadEvent = event;
    const rect = canvas.getBoundingClientRect();
    const scaleX = canvas.width / rect.width;
    const scaleY = canvas.height / rect.height;
    const x = Math.min(Math.max((event.clientX - rect.left) * scaleX, 0), canvas.width);
    const y = Math.min(Math.max((event.clientY - rect.top) * scaleY, 0), canvas.height);

    Module.touchEvent(phase, x / canvas.width, y / canvas.height);

    const ctx = canvas.getContext("2d");
    drawXypadGrid(ctx, canvas.width, canvas.height);
    if (phase !== Module.TouchEvent.Ended) {
      ctx.beginPath();
      ctx.arc(x, y, 6, 0, 2 * Math.PI);
      ctx.fillStyle = "#b6b2a1";
      ctx.fill();
      ctx.strokeStyle = "#b6b2a1";
      ctx.lineWidth = 1;
      ctx.stroke();
    }
  }

  canvas.addEventListener("pointerdown", (event) => {
    canvas.setPointerCapture(event.pointerId);
    ensureAudioRunning(container.querySelector(".preview-chip"));
    updateXYPad(event, Module.TouchEvent.Began);
  });

  canvas.addEventListener("pointermove", (event) => {
    if (!canvas.hasPointerCapture(event.pointerId)) {
      return;
    }
    updateXYPad(event, Module.TouchEvent.Moved);
  });

  canvas.addEventListener("pointerup", (event) => {
    canvas.releasePointerCapture(event.pointerId);
    if (!xyPadHold) {
      updateXYPad(event, Module.TouchEvent.Ended);
    }
  });
}

function connectWasmProcessor(context, wasmProcessor, plugin, build) {
  stopDryInputNodes();

  if (usesDryInput(plugin, build)) {
    const dryGain = new GainNode(context, { gain: 0.2 });
    const oscillator = new OscillatorNode(context, { frequency: 220, type: "sawtooth" });
    oscillator.connect(dryGain).connect(wasmProcessor);
    oscillator.start();
    dryInputNodes.push(oscillator, dryGain);
    return;
  }

  if (build.target === "nts-3_kaoss") {
    const silentGain = new GainNode(context, { gain: 0 });
    const silentSource = new ConstantSourceNode(context, { offset: 0 });
    silentSource.connect(silentGain).connect(wasmProcessor);
    silentSource.start();
    dryInputNodes.push(silentSource, silentGain);
  }
}

function wireKnobMappings(knobs, mappings) {
  const canvas = document.querySelector(".preview-xypad");
  if (!canvas) {
    return;
  }

  for (let paramIndex = 0; paramIndex < knobs.length; paramIndex += 1) {
    const mapping = mappings.get(paramIndex);
    const knob = knobs[paramIndex];
    if (!mapping || !knob) {
      continue;
    }

    knob.setValue(mapping.init);

    if (mapping.assign === Module.ParamAssign.X) {
      const handler = (event) => {
        if (event.buttons !== 1 && event.type !== "pointerdown") {
          return;
        }
        const rect = canvas.getBoundingClientRect();
        const xNormalized = Math.min(Math.max(event.clientX - rect.left, 0), rect.width) / rect.width;
        const curved = Module.applyCurveToParameter0to1(xNormalized, mapping.curve, mapping.unipolar);
        knob.setValue(curved * (knob.param.max - knob.param.min) + knob.param.min);
      };
      canvas.addEventListener("pointermove", handler);
      canvas.addEventListener("pointerdown", handler);
    } else if (mapping.assign === Module.ParamAssign.Y) {
      const handler = (event) => {
        if (event.buttons !== 1 && event.type !== "pointerdown") {
          return;
        }
        const rect = canvas.getBoundingClientRect();
        const yNormalized = 1.0 - Math.min(Math.max(event.clientY - rect.top, 0), rect.height) / rect.height;
        const curved = Module.applyCurveToParameter0to1(yNormalized, mapping.curve, mapping.unipolar);
        knob.setValue(curved * (knob.param.max - knob.param.min) + knob.param.min);
      };
      canvas.addEventListener("pointermove", handler);
      canvas.addEventListener("pointerdown", handler);
    }
  }
}

function buildSetupHandler(layout, knobsContainer, plugin, build, generation) {
  return function setupWebAudioAndUI(context, wasmProcessor) {
    if (generation !== previewGeneration) {
      return;
    }

    audioContext = context;
    const volume = new GainNode(context, { gain: 0.35 });

    const instrumentEl = document.getElementById("preview-instrument");
    const statusEl = document.getElementById("preview-status");
    statusEl.hidden = true;
    instrumentEl.hidden = false;

    if (layout === "keyboard") {
      const envelope = new GainNode(context, { gain: 0.0 });
      wasmProcessor.connect(envelope).connect(volume);
      mountOscInstrument(instrumentEl, context, envelope);
    } else {
      connectWasmProcessor(context, wasmProcessor, plugin, build);
      mountXypadInstrument(instrumentEl);
      wasmProcessor.connect(volume);
    }

    volume.connect(context.destination);

    knobsContainer.innerHTML = "";
    knobsContainer.hidden = false;

    const parameters = Module.getValidParameters();
    const mappings = layout === "xypad" ? Module.getDefaultMapping() : null;
    const knobs = [];

    for (let paramIndex = 0; paramIndex < parameters.size(); paramIndex += 1) {
      const param = parameters.get(paramIndex);
      const knob = createKnob(param, paramIndex, wasmProcessor);
      knobs.push(knob);
      knobsContainer.append(knob.element);
    }

    if (mappings) {
      wireKnobMappings(knobs, mappings);
    }
  };
}

export function pickPreviewBuild(plugin) {
  const builds = plugin.builds || [];
  return (
    builds.find((build) => build.target === "nts-1_mkii" && build.wasm) ||
    builds.find((build) => build.wasm) ||
    null
  );
}

function waitForPreviewReady(generation) {
  return new Promise((resolve, reject) => {
    const timeout = window.setTimeout(() => {
      reject(new Error("Preview timed out. Run scripts/sync-web-preview.sh after make wasm-ci."));
    }, PREVIEW_TIMEOUT_MS);

    const handler = (context, wasmProcessor) => {
      if (generation !== previewGeneration) {
        return;
      }
      window.clearTimeout(timeout);
      resolve({ context, wasmProcessor });
    };

    window.setupWebAudioAndUI = handler;
    globalThis.setupWebAudioAndUI = handler;
  });
}

function restorePreviewShellMarkup(shell) {
  shell.classList.remove("preview-shell--hosted");
  shell.innerHTML = `
    <p id="preview-status" class="preview-status">Select a plugin to preview.</p>
    <div id="preview-instrument" class="preview-instrument" hidden></div>
    <div id="preview-knobs" class="preview-knobs" hidden></div>
  `;
}

export async function runPreviewHost(build, plugin) {
  const generation = ++previewGeneration;

  const statusEl = document.getElementById("preview-status");
  const instrumentEl = document.getElementById("preview-instrument");
  const knobsEl = document.getElementById("preview-knobs");

  statusEl.hidden = false;
  statusEl.textContent = "Loading preview…";
  instrumentEl.hidden = true;
  knobsEl.hidden = true;
  instrumentEl.innerHTML = "";
  knobsEl.innerHTML = "";

  mountPlaceholderKnobs(knobsEl, plugin);

  if (!build?.wasm) {
    statusEl.textContent = "No WebAssembly preview for this plugin yet.";
    return;
  }

  const layout = previewLayout(build);
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

  try {
    const readyPromise = waitForPreviewReady(generation);
    await loadScript(jsUrl);
    const { context, wasmProcessor } = await readyPromise;
    buildSetupHandler(layout, knobsEl, plugin, build, generation)(context, wasmProcessor);
  } catch (error) {
    if (generation !== previewGeneration) {
      return;
    }
    statusEl.hidden = false;
    statusEl.textContent = error.message;
    instrumentEl.hidden = true;
  }
}

export async function mountPreview(build, plugin) {
  await teardownPreview();
  const mountToken = ++parentMountToken;

  const shell = document.getElementById("preview-shell");
  if (!shell) {
    return;
  }

  if (!build?.wasm) {
    restorePreviewShellMarkup(shell);
    const statusEl = document.getElementById("preview-status");
    statusEl.textContent = "No WebAssembly preview for this plugin yet.";
    return;
  }

  shell.classList.add("preview-shell--hosted");
  shell.innerHTML = "";

  const iframe = document.createElement("iframe");
  iframe.className = "preview-iframe";
  iframe.title = "Plugin preview";
  shell.append(iframe);
  activePreviewIframe = iframe;

  const params = new URLSearchParams({
    build: JSON.stringify(build),
    plugin: JSON.stringify(plugin),
  });

  await new Promise((resolve, reject) => {
    iframe.addEventListener("load", resolve, { once: true });
    iframe.addEventListener("error", reject, { once: true });
    iframe.src = `./preview-frame.html?${params.toString()}`;
  });

  if (mountToken !== parentMountToken) {
    return;
  }
}

export async function teardownPreview() {
  parentMountToken += 1;
  previewGeneration += 1;

  if (activePreviewIframe) {
    activePreviewIframe.remove();
    activePreviewIframe = null;
  }

  const shell = document.getElementById("preview-shell");
  if (shell?.classList.contains("preview-shell--hosted")) {
    restorePreviewShellMarkup(shell);
  }

  removeActiveScript();
  delete window.setupWebAudioAndUI;
  delete globalThis.setupWebAudioAndUI;

  frequencyStack = [];
  latchEnabled = false;
  xyPadHold = false;
  keyboard = null;
  stopDryInputNodes();

  const instrumentEl = document.getElementById("preview-instrument");
  if (instrumentEl?._cleanup) {
    instrumentEl._cleanup();
    delete instrumentEl._cleanup;
  }

  if (audioContext && audioContext.state !== "closed") {
    try {
      await audioContext.close();
    } catch {
      // Ignore close errors during teardown.
    }
  }
  audioContext = null;
  delete window.Module;
  delete globalThis.Module;
}
