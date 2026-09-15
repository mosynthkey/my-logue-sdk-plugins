<script setup>
import { computed, reactive, watch } from "vue";
import PreviewPanel from "./PreviewPanel.vue";
import { useI18n } from "../composables/useI18n.js";
import {
  canonicalPreviewTarget,
  downloadableBuilds,
  previewBuildForTarget,
  previewModeItems,
  sendableBuilds,
  targetName,
  unitFileName,
} from "../utils/plugin.js";

const props = defineProps({
  plugin: {
    type: Object,
    required: true,
  },
  activeTarget: {
    type: String,
    required: true,
  },
  connectedTargets: {
    type: Object,
    default: () => ({}),
  },
  inlineSlotsByTarget: {
    type: Object,
    default: () => ({}),
  },
  inlineSlotsLoading: {
    type: Object,
    default: () => ({}),
  },
  sending: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(["select-target", "send", "send-slot", "explain-dsp"]);
const { pluginDescription, t } = useI18n();

const activeBuild = computed(() => previewBuildForTarget(props.plugin, props.activeTarget));
const downloads = computed(() => downloadableBuilds(props.plugin));
const sends = computed(() => sendableBuilds(props.plugin));
const selectedSlotByTarget = reactive({});
const previewModes = computed(() =>
  previewModeItems(props.plugin).map((item) => ({
    value: item.value,
    title: item.mode === "xypad" ? t("previewXyPad") : t("previewKeyboard"),
  })),
);
const activePreviewMode = computed(() => canonicalPreviewTarget(props.activeTarget));

watch(
  () => props.inlineSlotsByTarget,
  (slotsByTarget) => {
    for (const [target, options] of Object.entries(slotsByTarget || {})) {
      const values = options.map((option) => option.value);
      if (!values.length) {
        continue;
      }
      if (!values.includes(selectedSlotByTarget[target])) {
        selectedSlotByTarget[target] = values.includes(1) ? 1 : values[0];
      }
    }
  },
  { deep: true, immediate: true },
);

function isTargetConnected(target) {
  return Boolean(props.connectedTargets[target]);
}

function slotsFor(target) {
  return props.inlineSlotsByTarget[target] || [];
}

function isSlotsLoading(target) {
  return Boolean(props.inlineSlotsLoading[target]);
}

function showInlineSlots(target) {
  return isTargetConnected(target)
    && (isSlotsLoading(target) || slotsFor(target).length > 0);
}

function slotSelectLabel(target) {
  if (target === "nts-1_mkii") return t("nts1Slot");
  if (target === "nts-3_kaoss") return t("nts3Slot");
  return t("slot");
}

function sendSelectedSlot(target) {
  const slotIndex = selectedSlotByTarget[target];
  if (slotIndex == null || Number.isNaN(slotIndex)) {
    return;
  }
  emit("send-slot", props.plugin, target, slotIndex);
}

function deviceNotFoundLabel(target) {
  return t("deviceNotFound", { target: targetName(target) });
}
</script>

<template>
  <div class="d-flex flex-column ga-6">
    <div>
      <h1 class="text-h4 font-weight-bold mb-2">{{ plugin.name }}</h1>
      <p class="text-body-large text-medium-emphasis mb-4">
        {{ pluginDescription(plugin) }}
      </p>
      <v-btn
        variant="tonal"
        prepend-icon="mdi-sitemap"
        @click="emit('explain-dsp', plugin)"
      >
        {{ t("dspHowItWorks") }}
      </v-btn>
    </div>

    <v-row v-if="downloads.length || sends.length">
      <v-col
        v-if="downloads.length"
        cols="12"
        md="6"
      >
        <h2 class="text-title-medium mb-3">{{ t("download") }}</h2>
        <div class="d-flex flex-wrap ga-2">
          <v-btn
            v-for="build in downloads"
            :key="`download-${build.target}`"
            :href="build.file"
            :download="unitFileName(build)"
            variant="tonal"
            prepend-icon="mdi-download"
          >
            {{ targetName(build.target) }}
          </v-btn>
        </div>
      </v-col>

      <v-col
        v-if="sends.length"
        cols="12"
        :md="downloads.length ? 6 : 12"
      >
        <h2 class="text-title-medium mb-3">{{ t("sendToDevice") }}</h2>

        <div class="d-flex flex-wrap align-center ga-2">
          <template
            v-for="build in sends"
            :key="`send-${build.target}`"
          >
            <template v-if="showInlineSlots(build.target)">
              <div class="d-flex align-center flex-wrap ga-2">
                <v-select
                  v-model="selectedSlotByTarget[build.target]"
                  :items="slotsFor(build.target)"
                  item-title="label"
                  item-value="value"
                  :label="slotSelectLabel(build.target)"
                  density="compact"
                  hide-details
                  :disabled="sending || isSlotsLoading(build.target)"
                  style="min-width: 14rem; max-width: 22rem;"
                />
                <v-btn
                  variant="tonal"
                  :disabled="sending || selectedSlotByTarget[build.target] == null"
                  @click="sendSelectedSlot(build.target)"
                >
                  {{ t("sendToSlot") }}
                </v-btn>
                <v-progress-circular
                  v-if="isSlotsLoading(build.target)"
                  indeterminate
                  size="16"
                  width="2"
                />
              </div>
            </template>
            <v-btn
              v-else
              variant="tonal"
              prepend-icon="mdi-usb"
              @click="emit('send', plugin, build.target)"
            >
              {{ deviceNotFoundLabel(build.target) }}
            </v-btn>
          </template>
        </div>
      </v-col>
    </v-row>

    <v-card
      variant="flat"
      class="pa-4"
    >
      <div class="d-flex flex-wrap align-center justify-space-between ga-3 mb-4">
        <h2 class="text-title-medium">{{ t("preview") }}</h2>
        <div
          v-if="previewModes.length > 1"
          class="d-flex flex-wrap ga-2"
        >
          <v-btn
            v-for="item in previewModes"
            :key="item.value"
            variant="tonal"
            :color="activePreviewMode === item.value ? 'primary' : undefined"
            :aria-pressed="activePreviewMode === item.value"
            @click="emit('select-target', item.value)"
          >
            {{ item.title }}
          </v-btn>
        </div>
      </div>

      <PreviewPanel
        :build="activeBuild"
        :plugin="plugin"
      />
    </v-card>
  </div>
</template>
