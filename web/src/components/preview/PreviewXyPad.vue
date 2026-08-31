<script setup>
import { onMounted, ref, watch } from "vue";
import { paintXypad, xyPadPointFromEvent } from "../../preview/geometry.js";

const props = defineProps({
  holdEnabled: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(["pointer-down", "pointer-move", "pointer-up"]);

const canvasEl = ref(null);
const lastPointer = ref(null);
let marker = null;

function redraw() {
  const canvas = canvasEl.value;
  if (!canvas) {
    return;
  }
  paintXypad(canvas, marker);
}

function onPointerDown(event) {
  canvasEl.value.setPointerCapture(event.pointerId);
  const position = xyPadPointFromEvent(canvasEl.value, event);
  lastPointer.value = position;
  marker = { x: position.x, y: position.y };
  redraw();
  emit("pointer-down", position);
}

function onPointerMove(event) {
  if (!canvasEl.value.hasPointerCapture(event.pointerId)) {
    return;
  }
  const position = xyPadPointFromEvent(canvasEl.value, event);
  lastPointer.value = position;
  marker = { x: position.x, y: position.y };
  redraw();
  emit("pointer-move", position);
}

function onPointerUp(event) {
  if (canvasEl.value.hasPointerCapture(event.pointerId)) {
    canvasEl.value.releasePointerCapture(event.pointerId);
  }
  const position = xyPadPointFromEvent(canvasEl.value, event);
  lastPointer.value = position;
  if (!props.holdEnabled) {
    marker = null;
    redraw();
    emit("pointer-up", position);
  }
}

watch(() => props.holdEnabled, (hold) => {
  if (!hold) {
    marker = null;
    redraw();
  }
});

defineExpose({
  lastPointer,
});

onMounted(() => {
  redraw();
  requestAnimationFrame(redraw);
});
</script>

<template>
  <div class="preview-xypad-shell">
    <canvas
      ref="canvasEl"
      class="preview-xypad"
      width="360"
      height="240"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointercancel="onPointerUp"
    />
  </div>
</template>
