<script setup>
import { computed, nextTick, ref } from "vue";
import {
  KNOB_ARC_START,
  KNOB_ARC_SWEEP,
  KNOB_CENTER,
  KNOB_RADIUS,
} from "../../preview/constants.js";
import {
  arcPath,
  knobAngle,
  knobValueFromDrag,
  normalizeKnobValue,
} from "../../preview/geometry.js";

const props = defineProps({
  knobId: {
    type: String,
    required: true,
  },
  name: {
    type: String,
    required: true,
  },
  min: {
    type: Number,
    required: true,
  },
  max: {
    type: Number,
    required: true,
  },
  value: {
    type: Number,
    required: true,
  },
  valueLabel: {
    type: String,
    required: true,
  },
  placeholder: {
    type: Boolean,
    default: false,
  },
  inputValue: {
    type: Number,
    default: null,
  },
  inputMin: {
    type: Number,
    default: null,
  },
  inputMax: {
    type: Number,
    default: null,
  },
});

const emit = defineEmits(["update:value"]);

const dragStartY = ref(0);
const dragStartValue = ref(0);
const isDragging = ref(false);
const isEditing = ref(false);
const editValue = ref("");
const inputRef = ref(null);

const DRAG_RANGE_PX = 160;

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
  emit("update:value", knobValueFromDrag(
    dragStartValue.value,
    dragStartY.value - event.clientY,
    props.min,
    props.max,
    DRAG_RANGE_PX,
  ));
}

function endDrag(event) {
  if (event.currentTarget.hasPointerCapture(event.pointerId)) {
    event.currentTarget.releasePointerCapture(event.pointerId);
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

function beginEditing(event) {
  event?.preventDefault();
  event?.stopPropagation();
  isEditing.value = true;
  editValue.value = String(props.inputValue ?? props.value);
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
    const inputMin = props.inputMin ?? props.min;
    const inputMax = props.inputMax ?? props.max;
    const clampedInput = Math.min(inputMax, Math.max(inputMin, parsed));
    const inputRange = inputMax - inputMin;
    const normalizedInput = inputRange === 0 ? 0 : (clampedInput - inputMin) / inputRange;
    emit("update:value", props.min + normalizedInput * (props.max - props.min));
  }
  isEditing.value = false;
}

function cancelEditing() {
  isEditing.value = false;
}
</script>

<template>
  <div class="knob" :class="{ 'knob--placeholder': placeholder }">
    <div
      class="knob__dial"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointercancel="onPointerCancel"
      @dblclick="beginEditing"
      :title="`Double-click to enter ${name} value`"
      @contextmenu.prevent
      @dragstart.prevent
    >
      <svg class="knob__svg" viewBox="0 0 48 48">
        <path class="knob__track" :d="trackPath" />
        <path class="knob__value-arc" :d="valueArcPath" />
      </svg>
      <span
        v-if="!isEditing"
        class="knob__center-value"
      >{{ valueLabel }}</span>
      <input
        v-else
        ref="inputRef"
        v-model="editValue"
        class="knob__value-input"
        type="text"
        inputmode="decimal"
        :aria-label="`Enter ${name} value between ${min} and ${max}`"
        @pointerdown.stop
        @dblclick.stop
        @keydown.enter.prevent="commitEditing"
        @keydown.escape.prevent="cancelEditing"
        @blur="commitEditing"
      >
    </div>
    <div class="knob__label">{{ name }}</div>
  </div>
</template>
