import { usesDryInput } from "./layout.js";

let dryInputNodes = [];

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
