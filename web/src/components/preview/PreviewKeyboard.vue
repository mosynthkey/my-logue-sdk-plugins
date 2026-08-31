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

function layoutMk2PadKeyboard(host) {
  const list = host.querySelector("ul");
  if (!list) {
    return;
  }

  const keys = [...list.children];
  let whiteCount = 0;
  for (const key of keys) {
    if (key.getAttribute("data-note-type") === "white") {
      whiteCount += 1;
      key.style.gridColumn = String(whiteCount);
      key.style.gridRow = "2";
    } else {
      key.style.gridColumn = String(whiteCount);
      key.style.gridRow = "1";
    }
  }

  // Hancock omits the last accidental; A-start 10 naturals needs the trailing C#.
  const lastKey = keys[keys.length - 1];
  const lastTitle = lastKey?.title || "";
  if (lastKey?.getAttribute("data-note-type") === "white" && lastTitle.startsWith("C") && lastTitle.charAt(1) !== "#") {
    const sharp = document.createElement("li");
    const octave = lastTitle.slice(1);
    sharp.id = `C#${octave}`;
    sharp.title = `C#${octave}`;
    sharp.setAttribute("data-note-type", "black");
    sharp.style.gridColumn = String(whiteCount);
    sharp.style.gridRow = "1";
    list.append(sharp);
  }

  host.style.setProperty("--pad-columns", String(whiteCount));
}

function mountKeyboard() {
  if (!props.enabled || !hostEl.value || keyboard || typeof QwertyHancock !== "function") {
    return;
  }

  const padGap = 10;
  const whiteKeyCount = 10;
  const availableWidth = Math.min(560, hostEl.value.clientWidth - 48 || 520);
  const padSize = Math.max(
    28,
    Math.floor((availableWidth - (whiteKeyCount - 1) * padGap) / (whiteKeyCount + 0.5)),
  );
  hostEl.value.style.setProperty("--pad-size", `${padSize}px`);
  hostEl.value.style.setProperty("--pad-gap", `${padGap}px`);

  keyboard = new QwertyHancock({
    id: "preview-keyboard",
    width: whiteKeyCount * (padSize + padGap),
    height: padSize,
    octaves: whiteKeyCount / 7,
    startNote: "A3",
    whiteKeyColour: "#e8e6df",
    blackKeyColour: "transparent",
    borderColour: "#b6b2a1",
    activeColour: "#b6b2a1",
  });
  layoutMk2PadKeyboard(hostEl.value);
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
