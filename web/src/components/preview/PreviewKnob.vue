<script setup>
import { computed, ref } from "vue";
import {
  KNOB_ARC_START,
  KNOB_ARC_SWEEP,
  KNOB_CENTER,
  KNOB_RADIUS,
} from "../../preview/constants.js";
import { arcPath, knobAngle, normalizeKnobValue, pointerPath } from "../../preview/geometry.js";

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
});

const emit = defineEmits(["update:value"]);

const dragStartY = ref(0);
const dragStartValue = ref(0);

const normalized = computed(() => normalizeKnobValue(props.value, props.min, props.max));
const angle = computed(() => knobAngle(normalized.value));
const valueArcPath = computed(() => {
  if (normalized.value <= 0.001) {
    return "";
  }
  return arcPath(KNOB_CENTER, KNOB_RADIUS, KNOB_ARC_START, angle.value);
});
const pointerArcPath = computed(() => pointerPath(KNOB_CENTER, KNOB_RADIUS, angle.value));
const trackPath = computed(() => arcPath(
  KNOB_CENTER,
  KNOB_RADIUS,
  KNOB_ARC_START,
  KNOB_ARC_START + KNOB_ARC_SWEEP,
));

function onPointerDown(event) {
  event.currentTarget.setPointerCapture(event.pointerId);
  dragStartY.value = event.clientY;
  dragStartValue.value = props.value;
}

function onPointerMove(event) {
  if (!event.currentTarget.hasPointerCapture(event.pointerId)) {
    return;
  }
  const delta = (dragStartY.value - event.clientY) * 0.4;
  emit("update:value", dragStartValue.value + delta);
}

function onPointerUp(event) {
  event.currentTarget.releasePointerCapture(event.pointerId);
}
</script>

<template>
  <div class="knob" :class="{ 'knob--placeholder': placeholder }">
    <div
      class="knob__dial"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
    >
      <svg class="knob__svg" viewBox="0 0 48 48">
        <defs>
          <linearGradient :id="`${knobId}-cap`" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="#3a3a3a" />
            <stop offset="100%" stop-color="#222222" />
          </linearGradient>
        </defs>
        <path class="knob__track" :d="trackPath" />
        <path class="knob__value-arc" :d="valueArcPath" />
        <circle
          class="knob__cap"
          :cx="KNOB_CENTER"
          :cy="KNOB_CENTER"
          r="13"
          :fill="`url(#${knobId}-cap)`"
        />
        <path class="knob__pointer" :d="pointerArcPath" />
      </svg>
    </div>
    <div class="knob__label">{{ name }}</div>
    <div class="knob__value">{{ valueLabel }}</div>
  </div>
</template>
