<script setup>
import { computed, nextTick, ref } from "vue";
import {
  KNOB_ARC_START,
  KNOB_ARC_SWEEP,
  KNOB_CENTER,
  KNOB_RADIUS,
} from "../preview/constants.js";
import {
  arcPath,
  knobAngle,
  knobValueFromDrag,
  normalizeKnobValue,
} from "../preview/geometry.js";

const props = defineProps({
  name: {
    type: String,
    required: true,
  },
  value: {
    type: Number,
    required: true,
  },
  min: {
    type: Number,
    default: 0,
  },
  max: {
    type: Number,
    default: 1023,
  },
});

const emit = defineEmits(["update:value"]);

const dragStartY = ref(0);
const dragStartValue = ref(0);
const isDragging = ref(false);
const isEditing = ref(false);
const editValue = ref("");
const inputRef = ref(null);
const DRAG_RANGE_PX = 140;

const normalized = computed(() => normalizeKnobValue(props.value, props.min, props.max));
const angle = computed(() => knobAngle(normalized.value));
const valueArcPath = computed(() => {
  if (normalized.value <= 0.001) {
    return "";
  }
  return arcPath(KNOB_CENTER, KNOB_RADIUS, KNOB_ARC_START, angle.value);
});
const trackPath = computed(() => arcPath(
  KNOB_CENTER,
  KNOB_RADIUS,
  KNOB_ARC_START,
  KNOB_ARC_START + KNOB_ARC_SWEEP,
));
const valueLabel = computed(() => String(Math.round(props.value)));

function onPointerDown(event) {
  if (event.button !== 0 || isEditing.value) {
    return;
  }
  event.preventDefault();
  event.currentTarget.setPointerCapture(event.pointerId);
  isDragging.value = true;
  dragStartY.value = event.clientY;
  dragStartValue.value = props.value;
}

function onPointerMove(event) {
  if (!isDragging.value || !event.currentTarget.hasPointerCapture(event.pointerId)) {
    return;
  }
  event.preventDefault();
  emit("update:value", Math.round(knobValueFromDrag(
    dragStartValue.value,
    dragStartY.value - event.clientY,
    props.min,
    props.max,
    DRAG_RANGE_PX,
  )));
}

function endDrag(event) {
  if (event.currentTarget.hasPointerCapture(event.pointerId)) {
    event.currentTarget.releasePointerCapture(event.pointerId);
  }
  isDragging.value = false;
}

function beginEditing(event) {
  event?.preventDefault();
  event?.stopPropagation();
  isEditing.value = true;
  editValue.value = String(Math.round(props.value));
  nextTick(() => {
    inputRef.value?.focus();
    inputRef.value?.select();
  });
}

function commitEditing() {
  if (!isEditing.value) {
    return;
  }
  const parsed = Number(editValue.value.trim());
  if (Number.isFinite(parsed)) {
    emit("update:value", Math.min(props.max, Math.max(props.min, Math.round(parsed))));
  }
  isEditing.value = false;
}

function cancelEditing() {
  isEditing.value = false;
}
</script>

<template>
  <div class="editor-knob">
    <div
      class="editor-knob__dial"
      :title="`Drag or double-click to edit ${name}`"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="endDrag"
      @pointercancel="endDrag"
      @dblclick="beginEditing"
      @contextmenu.prevent
      @dragstart.prevent
    >
      <svg
        class="editor-knob__svg"
        viewBox="0 0 48 48"
        aria-hidden="true"
      >
        <path
          class="editor-knob__track"
          :d="trackPath"
        />
        <path
          class="editor-knob__value-arc"
          :d="valueArcPath"
        />
      </svg>
      <span
        v-if="!isEditing"
        class="editor-knob__value"
      >{{ valueLabel }}</span>
      <input
        v-else
        ref="inputRef"
        v-model="editValue"
        class="editor-knob__input"
        type="text"
        inputmode="numeric"
        :aria-label="`${name} value`"
        @pointerdown.stop
        @dblclick.stop
        @keydown.enter.prevent="commitEditing"
        @keydown.escape.prevent="cancelEditing"
        @blur="commitEditing"
      >
    </div>
  </div>
</template>
