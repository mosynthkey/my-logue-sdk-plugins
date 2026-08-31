import { ref } from "vue";
import { previewDebugLog } from "./usePreviewDebugLog.js";
import { startWasmMainInGesture } from "../preview/wasm-runtime.js";

const unlocked = ref(false);
let unlockPromise = null;
let resolveUnlock = null;

function resetUnlockPromise() {
  unlockPromise = new Promise((resolve) => {
    resolveUnlock = resolve;
  });
}

resetUnlockPromise();

export function isAudioUnlocked() {
  return unlocked.value;
}

export function whenAudioUnlocked() {
  if (unlocked.value) {
    return Promise.resolve();
  }
  return unlockPromise;
}

export async function unlockAudioSession() {
  if (unlocked.value) {
    return;
  }

  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (AudioContextClass) {
    const unlockContext = new AudioContextClass();
    if (unlockContext.state === "suspended") {
      await unlockContext.resume();
    }
    await unlockContext.close();
  }

  unlocked.value = true;
  startWasmMainInGesture();
  previewDebugLog("info", "Audio session unlocked");
  resolveUnlock?.();
  resolveUnlock = null;
}

export function useAudioSession() {
  return {
    unlocked,
    unlockAudioSession,
    whenAudioUnlocked,
  };
}
