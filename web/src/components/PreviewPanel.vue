<script setup>
import { onBeforeUnmount, ref, watch } from "vue";
import PreviewDebugLog from "./preview/PreviewDebugLog.vue";
import PreviewKeyboard from "./preview/PreviewKeyboard.vue";
import PreviewKnob from "./preview/PreviewKnob.vue";
import PreviewXyPad from "./preview/PreviewXyPad.vue";
import { useWasmPreview } from "../preview/useWasmPreview.js";
import { getModule } from "../preview/wasm.js";

const props = defineProps({
  build: {
    type: Object,
    default: null,
  },
  plugin: {
    type: Object,
    default: null,
  },
});

const xyPadRef = ref(null);

const {
  message,
  layout,
  knobs,
  showInstrument,
  showKnobs,
  audioRunning,
  latchEnabled,
  holdEnabled,
  awaitingWasmTap,
  isReady,
  mount,
  teardown,
  setKnobValue,
  toggleAudio,
  onKeyboardDown,
  onKeyboardUp,
  onLatchToggle,
  onTouchEvent,
  onHoldToggle,
  startPreviewFromTap,
} = useWasmPreview();

watch(
  () => [props.build, props.plugin],
  ([build, plugin]) => {
    mount(build, plugin);
  },
  { immediate: true },
);

onBeforeUnmount(() => {
  teardown();
});

function onKnobUpdate(knobIndex, nextValue) {
  setKnobValue(knobIndex, nextValue, !knobs.value[knobIndex]?.placeholder);
}

function touchPhase(name) {
  return getModule()?.TouchEvent?.[name];
}

function onXyPointerDown(position) {
  const began = touchPhase("Began");
  if (began !== undefined) {
    onTouchEvent(began, position.xNormalized, position.yNormalized);
  }
}

function onXyPointerMove(position) {
  const moved = touchPhase("Moved");
  if (moved !== undefined) {
    onTouchEvent(moved, position.xNormalized, position.yNormalized);
  }
}

function onXyPointerUp(position) {
  const ended = touchPhase("Ended");
  if (ended !== undefined) {
    onTouchEvent(ended, position.xNormalized, position.yNormalized);
  }
}

function handleHoldToggle() {
  const lastPointer = xyPadRef.value?.lastPointer?.value ?? null;
  onHoldToggle(lastPointer);
}
</script>

<template>
  <section class="detail__stage" aria-label="Preview">
    <div class="preview-shell">
      <p
        v-if="!showInstrument && message"
        class="preview-status"
        :class="{ 'preview-status--action': awaitingWasmTap }"
        :role="awaitingWasmTap ? 'button' : undefined"
        :tabindex="awaitingWasmTap ? 0 : -1"
        @pointerdown="startPreviewFromTap"
        @keydown.enter.prevent="startPreviewFromTap"
        @keydown.space.prevent="startPreviewFromTap"
      >
        {{ message }}
      </p>

      <div v-show="showInstrument" class="preview-instrument">
        <div class="preview-toolbar">
          <button
            type="button"
            class="preview-chip"
            :class="{ 'is-on': audioRunning }"
            @click="toggleAudio"
          >
            {{ audioRunning ? "Suspend audio" : "Start audio" }}
          </button>

          <button
            v-if="layout === 'keyboard'"
            type="button"
            class="preview-chip"
            :class="{ 'is-on': latchEnabled }"
            @click="onLatchToggle"
          >
            Latch {{ latchEnabled ? "On" : "Off" }}
          </button>

          <button
            v-if="layout === 'xypad'"
            type="button"
            class="preview-chip"
            :class="{ 'is-on': holdEnabled }"
            @click="handleHoldToggle"
          >
            Hold {{ holdEnabled ? "On" : "Off" }}
          </button>
        </div>

        <PreviewKeyboard
          v-if="layout === 'keyboard'"
          :enabled="isReady"
          @note-down="onKeyboardDown"
          @note-up="onKeyboardUp"
        />

        <PreviewXyPad
          v-else
          ref="xyPadRef"
          :hold-enabled="holdEnabled"
          @pointer-down="onXyPointerDown"
          @pointer-move="onXyPointerMove"
          @pointer-up="onXyPointerUp"
        />
      </div>

      <div v-show="showKnobs" class="preview-knobs">
        <PreviewKnob
          v-for="(knob, knobIndex) in knobs"
          :key="`${knob.name}-${knobIndex}`"
          :knob-id="`knob-${knobIndex}`"
          :name="knob.name"
          :min="knob.min"
          :max="knob.max"
          :value="knob.value"
          :value-label="knob.valueLabel"
          :placeholder="knob.placeholder"
          @update:value="onKnobUpdate(knobIndex, $event)"
        />
      </div>

      <PreviewDebugLog />
    </div>
  </section>
</template>
