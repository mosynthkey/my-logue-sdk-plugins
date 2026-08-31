import { computed, ref, shallowRef } from "vue";
import { whenAudioUnlocked } from "../composables/useAudioSession.js";
import { AHREnvelopeTime } from "./constants.js";
import {
  connectWasmProcessor,
  ensureAudioRunning,
  stopDryInputNodes,
} from "./audio.js";
import { previewLayout } from "./layout.js";
import {
  clearWasmGlobals,
  configureWasmModule,
  loadWasmScript,
  waitForWasmReady,
} from "./wasm.js";

function approximatelyEqual(a, b, epsilon = 1e-4) {
  return Math.abs(a - b) < epsilon;
}

function buildPlaceholderKnobs(plugin) {
  return (plugin?.params || []).map((param) => ({
    name: param.name,
    min: 0,
    max: 100,
    value: 50,
    index: 0,
    placeholder: true,
    valueLabel: "50",
  }));
}

function readWasmKnobs(wasmProcessor) {
  const parameters = window.Module.getValidParameters();
  const knobs = [];
  for (let paramIndex = 0; paramIndex < parameters.size(); paramIndex += 1) {
    const param = parameters.get(paramIndex);
    const value = param.init;
    knobs.push({
      name: param.name,
      min: param.min,
      max: param.max,
      value,
      index: paramIndex,
      placeholder: false,
      valueLabel: formatKnobValue(paramIndex, value),
    });
  }
  return knobs;
}

function formatKnobValue(index, value) {
  if (window.Module?.getParameterValueString) {
    return window.Module.getParameterValueString(index, value);
  }
  return String(Math.round(value));
}

function readKnobMappings(knobCount) {
  const mappings = window.Module.getDefaultMapping();
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
  return entries;
}

export function useWasmPreview() {
  const phase = ref("idle");
  const message = ref("Select a plugin to preview.");
  const layout = ref("keyboard");
  const knobs = ref([]);
  const showInstrument = ref(false);
  const showKnobs = ref(false);
  const audioRunning = ref(false);
  const latchEnabled = ref(false);
  const holdEnabled = ref(false);

  const previewGeneration = ref(0);
  const audioContext = shallowRef(null);
  const wasmProcessor = shallowRef(null);
  const envelope = shallowRef(null);
  const knobMappings = shallowRef([]);

  const frequencyStack = ref([]);

  const isLoading = computed(() => phase.value === "loading");
  const isReady = computed(() => phase.value === "ready");
  const hasWasm = computed(() => phase.value !== "no-wasm");

  function resetPlaybackState() {
    frequencyStack.value = [];
    latchEnabled.value = false;
    holdEnabled.value = false;
    audioRunning.value = false;
  }

  function setKnobValue(knobIndex, nextValue, dispatch = true) {
    const knob = knobs.value[knobIndex];
    if (!knob) {
      return;
    }
    const clamped = Math.min(Math.max(nextValue, knob.min), knob.max);
    knob.value = clamped;
    knob.valueLabel = formatKnobValue(knob.index, clamped);
    if (dispatch && wasmProcessor.value) {
      const audioParameter = wasmProcessor.value.parameters.get(`${knob.index}`);
      if (audioParameter) {
        audioParameter.value = clamped;
      }
    }
  }

  function applyMappingValue(paramIndex, normalized, unipolar, curve) {
    const knobIndex = knobs.value.findIndex((knob) => knob.index === paramIndex);
    if (knobIndex < 0) {
      return;
    }
    const knob = knobs.value[knobIndex];
    const curved = window.Module.applyCurveToParameter0to1(normalized, curve, unipolar);
    setKnobValue(knobIndex, curved * (knob.max - knob.min) + knob.min);
  }

  function handleXyPosition(xNormalized, yNormalized) {
    for (const mapping of knobMappings.value) {
      if (mapping.assign === window.Module.ParamAssign.X) {
        applyMappingValue(mapping.paramIndex, xNormalized, mapping.unipolar, mapping.curve);
      } else if (mapping.assign === window.Module.ParamAssign.Y) {
        applyMappingValue(mapping.paramIndex, yNormalized, mapping.unipolar, mapping.curve);
      }
    }
  }

  function toggleAudio() {
    if (!audioContext.value) {
      return;
    }
    if (audioContext.value.state !== "running") {
      ensureAudioRunning(audioContext.value, audioRunning);
    } else {
      audioContext.value.suspend();
      audioRunning.value = false;
    }
  }

  function onKeyboardDown(note, frequency) {
    ensureAudioRunning(audioContext.value, audioRunning);
    frequencyStack.value = [...frequencyStack.value, frequency];
    window.Module.setOscPitch(frequency);
    window.Module.noteOn(note, 100);
    if (envelope.value) {
      envelope.value.gain.cancelAndHoldAtTime(audioContext.value.currentTime);
      envelope.value.gain.linearRampToValueAtTime(1.0, audioContext.value.currentTime + AHREnvelopeTime);
    }
  }

  function onKeyboardUp(note, frequency) {
    const nextStack = [...frequencyStack.value];
    for (let stackIndex = nextStack.length - 1; stackIndex >= 0; stackIndex -= 1) {
      if (approximatelyEqual(nextStack[stackIndex], frequency)) {
        nextStack.splice(stackIndex, 1);
        break;
      }
    }
    frequencyStack.value = nextStack;

    if (nextStack.length > 0) {
      window.Module.setOscPitch(nextStack[nextStack.length - 1]);
    } else if (!latchEnabled.value && envelope.value) {
      envelope.value.gain.cancelAndHoldAtTime(audioContext.value.currentTime);
      envelope.value.gain.linearRampToValueAtTime(0.0, audioContext.value.currentTime + AHREnvelopeTime);
    }

    window.Module.noteOff(note);
  }

  function onLatchToggle() {
    latchEnabled.value = !latchEnabled.value;
    if (!latchEnabled.value && frequencyStack.value.length === 0 && envelope.value && audioContext.value) {
      envelope.value.gain.cancelAndHoldAtTime(audioContext.value.currentTime);
      envelope.value.gain.linearRampToValueAtTime(0.0, audioContext.value.currentTime + AHREnvelopeTime);
    }
  }

  function onTouchEvent(phase, xNormalized, yNormalized) {
    window.Module.touchEvent(phase, xNormalized, yNormalized);
    handleXyPosition(xNormalized, yNormalized);
  }

  function onHoldToggle(lastPointerEvent) {
    holdEnabled.value = !holdEnabled.value;
    if (!holdEnabled.value && lastPointerEvent) {
      onTouchEvent(window.Module.TouchEvent.Ended, lastPointerEvent.xNormalized, lastPointerEvent.yNormalized);
    }
  }

  async function teardown() {
    previewGeneration.value += 1;
    clearWasmGlobals();
    stopDryInputNodes();
    resetPlaybackState();

    if (audioContext.value && audioContext.value.state !== "closed") {
      try {
        await audioContext.value.close();
      } catch {
        // Ignore close errors during teardown.
      }
    }

    audioContext.value = null;
    wasmProcessor.value = null;
    envelope.value = null;
    knobMappings.value = [];
    showInstrument.value = false;
    showKnobs.value = false;
    knobs.value = [];
  }

  async function mount(build, plugin) {
    await teardown();
    const generation = previewGeneration.value;

    if (!build?.wasm) {
      phase.value = "no-wasm";
      message.value = "No WebAssembly preview for this plugin yet.";
      knobs.value = buildPlaceholderKnobs(plugin);
      showKnobs.value = knobs.value.length > 0;
      showInstrument.value = false;
      return;
    }

    layout.value = previewLayout(build);
    phase.value = "loading";
    message.value = "Loading preview…";
    knobs.value = buildPlaceholderKnobs(plugin);
    showKnobs.value = knobs.value.length > 0;
    showInstrument.value = false;

    configureWasmModule(build);

    try {
      await whenAudioUnlocked();
      if (generation !== previewGeneration.value) {
        return;
      }

      phase.value = "loading";
      message.value = "Loading preview…";
      const readyPromise = waitForWasmReady(generation, previewGeneration);
      const { jsUrl } = configureWasmModule(build);
      await loadWasmScript(jsUrl);
      const { context, wasmProcessor: processor } = await readyPromise;
      if (generation !== previewGeneration.value) {
        return;
      }

      audioContext.value = context;
      wasmProcessor.value = processor;
      const volume = new GainNode(context, { gain: 0.35 });

      if (layout.value === "keyboard") {
        const gainEnvelope = new GainNode(context, { gain: 0.0 });
        envelope.value = gainEnvelope;
        processor.connect(gainEnvelope).connect(volume);
      } else {
        connectWasmProcessor(context, processor, plugin, build);
        processor.connect(volume);
      }

      volume.connect(context.destination);
      knobs.value = readWasmKnobs(processor);
      showKnobs.value = knobs.value.length > 0;

      if (layout.value === "xypad") {
        knobMappings.value = readKnobMappings(knobs.value.length);
        for (const mapping of knobMappings.value) {
          const knobIndex = knobs.value.findIndex((knob) => knob.index === mapping.paramIndex);
          if (knobIndex >= 0) {
            setKnobValue(knobIndex, mapping.init);
          }
        }
      }

      showInstrument.value = true;
      phase.value = "ready";
      message.value = "";
      ensureAudioRunning(context, audioRunning);
    } catch (error) {
      if (generation !== previewGeneration.value) {
        return;
      }
      phase.value = "error";
      message.value = error.message;
      showInstrument.value = false;
    }
  }

  return {
    phase,
    message,
    layout,
    knobs,
    showInstrument,
    showKnobs,
    audioRunning,
    latchEnabled,
    holdEnabled,
    isLoading,
    isReady,
    hasWasm,
    audioContext,
    wasmProcessor,
    mount,
    teardown,
    setKnobValue,
    toggleAudio,
    onKeyboardDown,
    onKeyboardUp,
    onLatchToggle,
    onTouchEvent,
    onHoldToggle,
    handleXyPosition,
  };
}
