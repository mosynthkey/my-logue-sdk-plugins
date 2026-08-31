<script setup>
import { onMounted, ref } from "vue";
import { drawXypadGrid } from "../../preview/geometry.js";

const props = defineProps({
  holdEnabled: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(["pointer-down", "pointer-move", "pointer-up"]);

const canvasEl = ref(null);
const lastPointer = ref(null);

function redraw(x, y, active) {
  const canvas = canvasEl.value;
  if (!canvas) {
    return;
  }
  const ctx = canvas.getContext("2d");
  drawXypadGrid(ctx, canvas.width, canvas.height);
  if (active) {
    ctx.beginPath();
    ctx.arc(x, y, 6, 0, 2 * Math.PI);
    ctx.fillStyle = "#b6b2a1";
    ctx.fill();
    ctx.strokeStyle = "#b6b2a1";
    ctx.lineWidth = 1;
    ctx.stroke();
  }
}

function pointerPosition(event) {
  const canvas = canvasEl.value;
  const rect = canvas.getBoundingClientRect();
  const scaleX = canvas.width / rect.width;
  const scaleY = canvas.height / rect.height;
  const x = Math.min(Math.max((event.clientX - rect.left) * scaleX, 0), canvas.width);
  const y = Math.min(Math.max((event.clientY - rect.top) * scaleY, 0), canvas.height);
  return {
    x,
    y,
    xNormalized: x / canvas.width,
    yNormalized: y / canvas.height,
  };
}

function onPointerDown(event) {
  canvasEl.value.setPointerCapture(event.pointerId);
  const position = pointerPosition(event);
  lastPointer.value = position;
  redraw(position.x, position.y, true);
  emit("pointer-down", position);
}

function onPointerMove(event) {
  if (!canvasEl.value.hasPointerCapture(event.pointerId)) {
    return;
  }
  const position = pointerPosition(event);
  lastPointer.value = position;
  redraw(position.x, position.y, true);
  emit("pointer-move", position);
}

function onPointerUp(event) {
  canvasEl.value.releasePointerCapture(event.pointerId);
  const position = pointerPosition(event);
  lastPointer.value = position;
  redraw(position.x, position.y, false);
  if (!props.holdEnabled) {
    emit("pointer-up", position);
  }
}

defineExpose({
  lastPointer,
});

onMounted(() => {
  redraw(0, 0, false);
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
    />
  </div>
</template>
