import { ref } from "vue";
import { previewDebugLog } from "./usePreviewDebugLog.js";
import { needsGestureForWasmStart } from "../preview/gesture.js";

const unlocked = ref(false);
let unlockPromise = null;
let resolveUnlock = null;

function resetUnlockPromise() {
  unlockPromise = new Promise((resolve) => {
    resolveUnlock = resolve;
  });
}

resetUnlockPromise();

if (!needsGestureForWasmStart()) {
  unlocked.value = true;
  resolveUnlock?.();
  resolveUnlock = null;
}

export function isAudioUnlocked() {
  return unlocked.value;
}

export function whenAudioUnlocked() {
  if (unlocked.value) {
    return Promise.resolve();
  }
  return unlockPromise;
}

export function unlockAudioSessionSync() {
  if (unlocked.value) {
    return;
  }

  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (AudioContextClass) {
    const unlockContext = new AudioContextClass();
    if (unlockContext.state === "suspended") {
      unlockContext.resume();
    }
    window.setTimeout(() => {
      if (unlockContext.state !== "closed") {
        unlockContext.close();
      }
    }, 1000);
  }

  unlocked.value = true;
  previewDebugLog("info", "Audio session unlocked");
  resolveUnlock?.();
  resolveUnlock = null;
}

export function useAudioSession() {
  return {
    unlocked,
    unlockAudioSessionSync,
    whenAudioUnlocked,
  };
}
