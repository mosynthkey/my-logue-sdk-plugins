import { usesDryInput } from "./layout.js";

let dryInputNodes = [];
let audioUnlockContext = null;

export function previewNeedsUserGesture() {
  const mobileLayout = window.matchMedia("(max-width: 820px), (pointer: coarse)").matches;
  return mobileLayout || !window.crossOriginIsolated;
}

export function unlockAudioForPreview() {
  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (!AudioContextClass) {
    return Promise.resolve();
  }

  if (!audioUnlockContext) {
    audioUnlockContext = new AudioContextClass();
  }

  if (audioUnlockContext.state === "suspended") {
    return audioUnlockContext.resume();
  }

  return Promise.resolve();
}

export async function closeAudioUnlockContext() {
  if (audioUnlockContext && audioUnlockContext.state !== "closed") {
    try {
      await audioUnlockContext.close();
    } catch {
      // Ignore close errors during teardown.
    }
  }
  audioUnlockContext = null;
}

export function stopDryInputNodes() {
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

export function connectWasmProcessor(context, wasmProcessor, plugin, build) {
  stopDryInputNodes();

  if (usesDryInput(plugin, build)) {
    const dryGain = new GainNode(context, { gain: 0.2 });
    const oscillator = new OscillatorNode(context, { frequency: 220, type: "sawtooth" });
    oscillator.connect(dryGain).connect(wasmProcessor);
    oscillator.start();
    dryInputNodes.push(oscillator, dryGain);
    return;
  }

  if (build?.target === "nts-3_kaoss") {
    const silentGain = new GainNode(context, { gain: 0 });
    const silentSource = new ConstantSourceNode(context, { offset: 0 });
    silentSource.connect(silentGain).connect(wasmProcessor);
    silentSource.start();
    dryInputNodes.push(silentSource, silentGain);
  }
}

export function ensureAudioRunning(audioContext, audioRunningRef) {
  if (!audioContext) {
    return;
  }
  if (audioContext.state !== "running") {
    audioContext.resume();
  }
  audioRunningRef.value = true;
}
