<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from "vue";

const props = defineProps({
  enabled: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(["note-down", "note-up"]);

const hostEl = ref(null);
let keyboard = null;
let currentOctave = 3;

function mountKeyboard() {
  if (!props.enabled || !hostEl.value || keyboard || typeof QwertyHancock !== "function") {
    return;
  }

  const keyboardWidth = Math.min(520, hostEl.value.clientWidth - 32 || 520);
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
  keyboard.keyDown = (note, frequency) => emit("note-down", note, frequency);
  keyboard.keyUp = (note, frequency) => emit("note-up", note, frequency);
}

function destroyKeyboard() {
  if (!keyboard) {
    return;
  }
  keyboard.keyDown = () => {};
  keyboard.keyUp = () => {};
  if (hostEl.value) {
    hostEl.value.innerHTML = "";
  }
  keyboard = null;
}

function onOctaveKeyDown(event) {
  if (!keyboard) {
    return;
  }
  if (event.key === "z") {
    currentOctave = Math.max(0, currentOctave - 1);
    keyboard.setKeyOctave(currentOctave);
  } else if (event.key === "x") {
    currentOctave = Math.min(7, currentOctave + 1);
    keyboard.setKeyOctave(currentOctave);
  }
}

watch(() => props.enabled, (enabled) => {
  if (enabled) {
    mountKeyboard();
  } else {
    destroyKeyboard();
  }
});

onMounted(() => {
  document.addEventListener("keydown", onOctaveKeyDown);
  mountKeyboard();
});

onBeforeUnmount(() => {
  document.removeEventListener("keydown", onOctaveKeyDown);
  destroyKeyboard();
});
</script>

<template>
  <div class="preview-keyboard-shell">
    <div class="preview-keyboard-wrap preview-keyboard-wrap--mk2">
      <div
        id="preview-keyboard"
        ref="hostEl"
      />
    </div>
  </div>
</template>
