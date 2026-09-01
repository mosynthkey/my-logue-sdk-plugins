<script setup>
import { onBeforeUnmount, ref, watch } from "vue";
import PreviewDebugLog from "./preview/PreviewDebugLog.vue";
import PreviewDepthPad from "./preview/PreviewDepthPad.vue";
import PreviewKeyboard from "./preview/PreviewKeyboard.vue";
import PreviewKnob from "./preview/PreviewKnob.vue";
import PreviewScope from "./preview/PreviewScope.vue";
import PreviewXyPad from "./preview/PreviewXyPad.vue";
import { useWasmPreview } from "../preview/useWasmPreview.js";
import { useI18n } from "../composables/useI18n.js";

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
const { t } = useI18n();

const xyPadRef = ref(null);

const previewShellRef = ref(null);

const {
  message,
  layout,
  knobs,
  showInstrument,
  showKnobs,
  latchEnabled,
  holdEnabled,
  masterVolume,
  bpm,
  depthNormalized,
  hasDepthMapping,
  masterVolumeLabel,
  bpmLabel,
  awaitingWasmTap,
  kickDemoActive,
  isReady,
  mount,
  teardown,
  setKnobValue,
  setMasterVolume,
  setBpm,
  handleDepthChange,
  readScopeSnapshot,
  onKeyboardDown,
  onKeyboardUp,
  onLatchToggle,
  onTouchBegan,
  onTouchMoved,
  onTouchEnded,
  onHoldToggle,
  startPreviewFromTap,
} = useWasmPreview(previewShellRef);

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

function onXyPointerDown(position) {
  onTouchBegan(position.xNormalized, position.yNormalized);
}

function onXyPointerMove(position) {
  onTouchMoved(position.xNormalized, position.yNormalized);
}

function onXyPointerUp(position) {
  onTouchEnded(position.xNormalized, position.yNormalized);
}

function handleHoldToggle() {
  const lastPointer = xyPadRef.value?.lastPointer?.value ?? null;
  onHoldToggle(lastPointer);
}

</script>

<template>
  <section class="detail__stage" :aria-label="t('preview')">
    <div ref="previewShellRef" class="preview-shell">
      <p
        v-if="!showInstrument && message"
        class="preview-status"
        :class="{ 'preview-status--action': awaitingWasmTap }"
        :role="awaitingWasmTap ? 'button' : undefined"
        :tabindex="awaitingWasmTap ? 0 : -1"
        @keydown.enter.prevent="startPreviewFromTap"
        @keydown.space.prevent="startPreviewFromTap"
      >
        {{ message }}
      </p>

      <div
        v-if="showInstrument"
        class="preview-toolbar"
      >
        <div class="preview-toolbar__controls">
          <button
            v-if="showInstrument && layout === 'keyboard' && !kickDemoActive"
            type="button"
            class="preview-chip"
            :class="{ 'is-on': latchEnabled }"
            @click="onLatchToggle"
          >
            {{ t("latch") }} {{ t(latchEnabled ? "on" : "off") }}
          </button>

          <button
            v-if="showInstrument && layout === 'xypad'"
            type="button"
            class="preview-chip"
            :class="{ 'is-on': holdEnabled }"
            @click="handleHoldToggle"
          >
            Hold {{ holdEnabled ? "On" : "Off" }}
          </button>
        </div>
      </div>

      <div v-if="showInstrument" class="preview-instrument">
        <div class="preview-instrument-row">
          <div
            v-if="layout === 'keyboard' && !kickDemoActive"
            class="preview-instrument-main"
          >
            <PreviewKeyboard
              :enabled="isReady"
              @note-down="onKeyboardDown"
              @note-up="onKeyboardUp"
            />
          </div>

          <div
            v-else
            class="preview-xypad-group"
          >
            <PreviewDepthPad
              v-if="hasDepthMapping"
              :depth-normalized="depthNormalized"
              @update:depth="handleDepthChange"
            />

            <PreviewXyPad
              ref="xyPadRef"
              :hold-enabled="holdEnabled"
              @pointer-down="onXyPointerDown"
              @pointer-move="onXyPointerMove"
              @pointer-up="onXyPointerUp"
            />
          </div>

          <PreviewScope
            :enabled="isReady"
            :read-snapshot="readScopeSnapshot"
          />
        </div>
      </div>

      <div v-if="showInstrument" class="preview-knobs">
        <PreviewKnob
          knob-id="master-volume"
          name="[Volume]"
          :min="0"
          :max="1"
          :value="masterVolume"
          :value-label="masterVolumeLabel"
          :input-value="masterVolume * 100"
          :input-min="0"
          :input-max="100"
          @update:value="setMasterVolume"
        />

        <PreviewKnob
          knob-id="master-bpm"
          name="[BPM]"
          :min="30"
          :max="240"
          :value="bpm"
          :value-label="bpmLabel"
          @update:value="setBpm"
        />

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
