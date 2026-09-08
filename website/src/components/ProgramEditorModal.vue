<script setup>
import { computed } from "vue";
import { useDisplay } from "vuetify";
import EditorKnob from "./EditorKnob.vue";
import RoutingDiagram from "./RoutingDiagram.vue";
import { useI18n } from "../composables/useI18n.js";
import {
  ASSIGN_OPTIONS,
  CURVE_OPTIONS,
  INPUT_SELECT_OPTIONS,
  RELEASE_MODE_OPTIONS,
  ROUTING_OPTIONS,
  SLOT_FX_OPTIONS,
} from "../composables/useProgramEditor.js";

const props = defineProps({
  isOpen: {
    type: Boolean,
    required: true,
  },
  program: {
    type: Object,
    required: true,
  },
});

const emit = defineEmits([
  "close",
  "receive",
  "select-slot",
  "update:routing",
  "clear-slot",
  "update-slot",
  "update-param",
]);

const { t } = useI18n();
const { mdAndUp } = useDisplay();

const activeSlot = computed(() => props.program.slots[props.program.activeSlot]);
const paramGridClass = computed(() => {
  if (mdAndUp.value) {
    return "program-editor__params program-editor__params--wide";
  }
  return "program-editor__params";
});

const curveItems = CURVE_OPTIONS.map((curveName, curveIndex) => ({
  title: `${curveIndex}: ${curveName}`,
  value: curveIndex,
}));

const inputSelectItems = INPUT_SELECT_OPTIONS.map((label, optionIndex) => ({
  title: `${optionIndex}: ${label}`,
  value: optionIndex,
}));

const releaseModeItems = RELEASE_MODE_OPTIONS.map((label, optionIndex) => ({
  title: `${optionIndex}: ${label}`,
  value: optionIndex,
}));

const slotFxItems = SLOT_FX_OPTIONS.map((fxLabel, fxIndex) => ({
  title: fxLabel,
  value: fxIndex,
}));

function slotLabel(slotIndex) {
  return `FX${slotIndex + 1}`;
}
</script>

<template>
  <v-dialog
    :model-value="isOpen"
    fullscreen
    transition="dialog-bottom-transition"
    @update:model-value="(value) => !value && emit('close')"
  >
    <v-card
      class="d-flex flex-column"
      color="background"
      flat
    >
      <v-toolbar
        density="comfortable"
        color="background"
        flat
      >
        <v-btn
          icon="mdi-close"
          :aria-label="t('close')"
          @click="emit('close')"
        />
        <v-toolbar-title>
          <div class="text-label-large text-medium-emphasis">{{ t("programEditorKicker") }}</div>
          <div>{{ t("programEditorTitle") }}</div>
        </v-toolbar-title>
        <v-spacer />
        <v-btn
          color="primary"
          variant="flat"
          prepend-icon="mdi-download"
          class="me-2"
          @click="emit('receive')"
        >
          Receive
        </v-btn>
      </v-toolbar>

      <v-card-text class="flex-grow-1 overflow-y-auto">
        <div class="mb-4">
          <div class="text-label-large mb-2">{{ t("routing") }}</div>
          <div class="program-editor__routing">
            <button
              v-for="option in ROUTING_OPTIONS"
              :key="option.id"
              type="button"
              class="program-editor__routing-option"
              :class="{ 'is-active': program.routing === option.id }"
              :title="`${option.label} (${option.hint})`"
              :aria-label="`${option.label}: ${option.hint}`"
              @click="emit('update:routing', option.id)"
            >
              <RoutingDiagram :routing-id="option.id" />
              <span>{{ option.label }}</span>
            </button>
          </div>
        </div>

        <v-tabs
          :model-value="program.activeSlot"
          color="primary"
          class="mb-4"
          @update:model-value="emit('select-slot', $event)"
        >
          <v-tab
            v-for="(slot, slotIndex) in program.slots"
            :key="slotIndex"
            :value="slotIndex"
          >
            {{ slotLabel(slotIndex) }}
          </v-tab>
        </v-tabs>

        <div class="d-flex flex-wrap align-center ga-3 mb-4">
          <v-switch
            :model-value="activeSlot.on"
            color="success"
            hide-details
            density="compact"
            :label="t('on')"
            @update:model-value="emit('update-slot', { on: $event })"
          />
          <v-select
            :model-value="activeSlot.fxIndex"
            :items="slotFxItems"
            :label="t('slotFx')"
            density="compact"
            hide-details
            variant="underlined"
            style="max-width: 240px;"
            @update:model-value="emit('update-slot', { fxIndex: $event })"
          />
          <v-spacer />
          <v-btn
            variant="text"
            @click="emit('clear-slot')"
          >
            {{ t("clearSlot") }}
          </v-btn>
        </div>

        <div :class="paramGridClass">
          <div
            v-for="(param, paramIndex) in activeSlot.params"
            :key="`${program.activeSlot}-${param.name}`"
            class="program-editor__param"
          >
            <div class="text-title-small mb-2 text-center">{{ param.name }}</div>
            <div class="d-flex justify-center mb-3">
              <EditorKnob
                :name="param.name"
                :value="param.value"
                :min="0"
                :max="1023"
                @update:value="emit('update-param', paramIndex, { value: $event })"
              />
            </div>
            <v-row dense>
              <v-col cols="6">
                <v-text-field
                  :model-value="param.min"
                  type="number"
                  label="MIN"
                  density="compact"
                  hide-details
                  variant="underlined"
                  @update:model-value="emit('update-param', paramIndex, { min: Number($event) })"
                />
              </v-col>
              <v-col cols="6">
                <v-text-field
                  :model-value="param.max"
                  type="number"
                  label="MAX"
                  density="compact"
                  hide-details
                  variant="underlined"
                  @update:model-value="emit('update-param', paramIndex, { max: Number($event) })"
                />
              </v-col>
            </v-row>
            <v-select
              :model-value="param.curve"
              :items="curveItems"
              label="CURVE"
              density="compact"
              hide-details
              variant="underlined"
              class="mt-2"
              @update:model-value="emit('update-param', paramIndex, { curve: $event })"
            />
            <div class="text-label-medium mt-3 mb-1">POLARITY</div>
            <v-btn-toggle
              :model-value="param.polarity"
              mandatory
              density="compact"
              color="primary"
              divided
              class="w-100"
              @update:model-value="emit('update-param', paramIndex, { polarity: $event })"
            >
              <v-btn
                value="uni"
                size="small"
                class="flex-grow-1"
                variant="text"
              >
                UNI
              </v-btn>
              <v-btn
                value="bi"
                size="small"
                class="flex-grow-1"
                variant="text"
              >
                BI
              </v-btn>
            </v-btn-toggle>
            <div class="text-label-medium mt-3 mb-1">ASSIGN</div>
            <v-btn-toggle
              :model-value="param.assign"
              mandatory
              density="compact"
              color="primary"
              divided
              class="w-100"
              @update:model-value="emit('update-param', paramIndex, { assign: $event })"
            >
              <v-btn
                v-for="assignOption in ASSIGN_OPTIONS"
                :key="assignOption"
                :value="assignOption"
                size="small"
                class="flex-grow-1"
                variant="text"
              >
                {{ assignOption }}
              </v-btn>
            </v-btn-toggle>
          </div>
        </div>

        <v-divider class="my-6 opacity-25" />

        <v-row>
          <v-col
            cols="12"
            md="6"
          >
            <v-row dense>
              <v-col cols="12">
                <v-select
                  :model-value="activeSlot.inputSelect"
                  :items="inputSelectItems"
                  :label="t('fxInSelect')"
                  density="compact"
                  hide-details
                  variant="underlined"
                  style="max-width: 240px;"
                  @update:model-value="emit('update-slot', { inputSelect: $event })"
                />
              </v-col>
              <v-col cols="12">
                <v-select
                  :model-value="activeSlot.releaseMode"
                  :items="releaseModeItems"
                  :label="t('fxReleaseMode')"
                  density="compact"
                  hide-details
                  variant="underlined"
                  style="max-width: 240px;"
                  @update:model-value="emit('update-slot', { releaseMode: $event })"
                />
              </v-col>
              <v-col
                cols="12"
                sm="6"
              >
                <div class="text-label-medium mb-2 text-center">{{ t("fxReleaseTime") }}</div>
                <div class="d-flex justify-center">
                  <EditorKnob
                    :name="t('fxReleaseTime')"
                    :value="activeSlot.releaseTime"
                    :min="0"
                    :max="1023"
                    @update:value="emit('update-slot', { releaseTime: $event })"
                  />
                </div>
              </v-col>
              <v-col
                cols="12"
                sm="6"
              >
                <div class="text-label-medium mb-2 text-center">{{ t("outGain") }}</div>
                <div class="d-flex justify-center">
                  <EditorKnob
                    :name="t('outGain')"
                    :value="activeSlot.outGain"
                    :min="0"
                    :max="1023"
                    @update:value="emit('update-slot', { outGain: $event })"
                  />
                </div>
              </v-col>
            </v-row>
          </v-col>

          <v-col
            cols="12"
            md="6"
          >
            <div class="d-flex align-stretch ga-4">
              <div class="d-flex flex-column ga-2 justify-center">
                <v-switch
                  :model-value="activeSlot.xyFreeze"
                  color="primary"
                  hide-details
                  density="compact"
                  label="XY FREEZE"
                  @update:model-value="emit('update-slot', { xyFreeze: $event })"
                />
                <v-switch
                  :model-value="activeSlot.depthFreeze"
                  color="primary"
                  hide-details
                  density="compact"
                  label="DEPTH FREEZE"
                  @update:model-value="emit('update-slot', { depthFreeze: $event })"
                />
              </div>
              <div>
                <div class="text-label-medium mb-2">
                  DEPTH {{ activeSlot.depth }} · Y {{ activeSlot.y }} · X {{ activeSlot.x }}
                </div>
                <div class="program-editor__pad">
                  <div class="program-editor__depth" aria-hidden="true">
                    <span
                      class="program-editor__depth-fill"
                      :style="{ height: `${(activeSlot.depth / 1023) * 100}%` }"
                    />
                  </div>
                  <div class="program-editor__xy" aria-hidden="true">
                    <span
                      class="program-editor__xy-dot"
                      :style="{
                        left: `${(activeSlot.x / 1023) * 100}%`,
                        top: `${100 - (activeSlot.y / 1023) * 100}%`,
                      }"
                    />
                  </div>
                </div>
              </div>
            </div>
          </v-col>
        </v-row>
      </v-card-text>
    </v-card>
  </v-dialog>
</template>
