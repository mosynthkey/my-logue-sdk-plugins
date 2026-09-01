<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import { depthPadPointFromEvent, paintDepthPad } from "../../preview/geometry.js";

const props = defineProps({
  depthNormalized: {
    type: Number,
    default: 0.5,
  },
});

const emit = defineEmits(["update:depth"]);

const canvasEl = ref(null);
const isDragging = ref(false);
let resizeObserver = null;

function markerYFromDepth(depthNormalized) {
  const canvas = canvasEl.value;
  if (!canvas) {
    return null;
  }
  const rect = canvas.getBoundingClientRect();
  return (1 - depthNormalized) * rect.height;
}

function redraw() {
  const canvas = canvasEl.value;
  if (!canvas) {
    return;
  }
  paintDepthPad(canvas, markerYFromDepth(props.depthNormalized));
}

function emitDepthFromEvent(event) {
  const position = depthPadPointFromEvent(canvasEl.value, event);
  emit("update:depth", position.depthNormalized);
}

function onPointerDown(event) {
  event.preventDefault();
  canvasEl.value.setPointerCapture(event.pointerId);
  isDragging.value = true;
  emitDepthFromEvent(event);
}

function onPointerMove(event) {
  if (!isDragging.value || !canvasEl.value.hasPointerCapture(event.pointerId)) {
    return;
  }
  event.preventDefault();
  emitDepthFromEvent(event);
}

function endDrag(event) {
  if (canvasEl.value?.hasPointerCapture(event.pointerId)) {
    canvasEl.value.releasePointerCapture(event.pointerId);
  }
  isDragging.value = false;
}

function onPointerUp(event) {
  event.preventDefault();
  endDrag(event);
}

function onPointerCancel(event) {
  event.preventDefault();
  endDrag(event);
}

watch(() => props.depthNormalized, () => {
  redraw();
});

onMounted(() => {
  const canvas = canvasEl.value;
  if (canvas && typeof ResizeObserver === "function") {
    resizeObserver = new ResizeObserver(() => {
      redraw();
    });
    resizeObserver.observe(canvas);
  }
  redraw();
});

onBeforeUnmount(() => {
  resizeObserver?.disconnect();
  resizeObserver = null;
});
</script>

<template>
  <div class="preview-depth-shell">
    <div class="preview-depth-label">Depth</div>
    <canvas
      ref="canvasEl"
      class="preview-depth-pad"
      aria-label="Depth"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointercancel="onPointerCancel"
      @contextmenu.prevent
      @dragstart.prevent
    />
  </div>
</template>
