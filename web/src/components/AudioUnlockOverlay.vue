<script setup>
import { useAudioSession } from "../composables/useAudioSession.js";

const { unlocked, unlockAudioSession } = useAudioSession();
</script>

<template>
  <div
    v-if="!unlocked"
    class="audio-unlock"
    role="dialog"
    aria-modal="true"
    aria-labelledby="audio-unlock-title"
  >
    <div class="audio-unlock__card">
      <h2 id="audio-unlock-title" class="audio-unlock__title">Start audio</h2>
      <p class="audio-unlock__text">
        Tap once to enable WebAssembly preview and audio playback in your browser.
      </p>
      <button
        type="button"
        class="button button-primary audio-unlock__button"
        @click="unlockAudioSession"
      >
        Start audio
      </button>
    </div>
  </div>
</template>
