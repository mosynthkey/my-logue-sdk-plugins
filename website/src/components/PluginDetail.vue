<script setup>
import { computed, reactive, watch } from "vue";
import PreviewPanel from "./PreviewPanel.vue";
import { useI18n } from "../composables/useI18n.js";
import {
  buildForTarget,
  downloadableBuilds,
  moduleFor,
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
  inlineStatusText: {
    type: String,
    default: "",
  },
  inlineStatusKind: {
    type: String,
    default: "idle",
  },
  sending: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(["select-target", "send", "send-slot", "explain-dsp"]);
const { pluginDescription, t } = useI18n();

const activeBuild = computed(() => buildForTarget(props.plugin, props.activeTarget));
const downloads = computed(() => downloadableBuilds(props.plugin));
const sends = computed(() => sendableBuilds(props.plugin));
const selectedSlotByTarget = reactive({});

const targetItems = computed(() => {
  const targets = new Set([
    ...downloads.value.map((build) => build.target),
    ...sends.value.map((build) => build.target),
    ...(props.plugin.targets || []),
  ]);
  if (props.activeTarget) {
    targets.add(props.activeTarget);
  }
  return [...targets].map((target) => ({
    value: target,
    title: targetName(target),
  }));
});

const inlineStatusColor = computed(() => {
  if (props.inlineStatusKind === "ok") return "success";
  if (props.inlineStatusKind === "error") return "error";
  if (props.inlineStatusKind === "warn") return "warning";
  if (props.inlineStatusKind === "busy") return "info";
  return "secondary";
});

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

function moduleLabel(target) {
  return moduleFor(props.plugin, target);
}

function sendSelectedSlot(target) {
  const slotIndex = selectedSlotByTarget[target];
  if (slotIndex == null || Number.isNaN(slotIndex)) {
    return;
  }
  emit("send-slot", props.plugin, target, slotIndex);
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
            variant="outlined"
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

        <v-alert
          v-if="inlineStatusText"
          :color="inlineStatusColor"
          variant="tonal"
          density="compact"
          class="mb-3"
          :text="inlineStatusText"
        />

        <div class="d-flex flex-column ga-4">
          <div
            v-for="build in sends"
            :key="`send-${build.target}`"
          >
            <template v-if="showInlineSlots(build.target)">
              <div class="d-flex align-center flex-wrap ga-2 mb-2">
                <span class="text-label-large">{{ targetName(build.target) }}</span>
                <span class="text-body-small text-medium-emphasis">
                  {{ t("sendSlotHint", { module: moduleLabel(build.target) }) }}
                </span>
                <v-progress-circular
                  v-if="isSlotsLoading(build.target)"
                  indeterminate
                  size="16"
                  width="2"
                />
              </div>
              <div class="d-flex align-center flex-wrap ga-2">
                <v-select
                  v-model="selectedSlotByTarget[build.target]"
                  :items="slotsFor(build.target)"
                  item-title="label"
                  item-value="value"
                  :label="t('slot')"
                  density="compact"
                  hide-details
                  :disabled="sending || isSlotsLoading(build.target)"
                  style="min-width: 14rem; max-width: 22rem;"
                />
                <v-btn
                  color="primary"
                  variant="flat"
                  :disabled="sending || selectedSlotByTarget[build.target] == null"
                  @click="sendSelectedSlot(build.target)"
                >
                  {{ t("sendToSlot") }}
                </v-btn>
              </div>
            </template>
            <v-btn
              v-else
              variant="outlined"
              prepend-icon="mdi-usb"
              @click="emit('send', plugin, build.target)"
            >
              {{ targetName(build.target) }}
            </v-btn>
          </div>
        </div>
      </v-col>
    </v-row>

    <v-card
      variant="flat"
      class="pa-4"
    >
      <div class="d-flex flex-wrap align-center justify-space-between ga-3 mb-4">
        <h2 class="text-title-medium">{{ t("preview") }}</h2>
        <v-btn-toggle
          v-if="targetItems.length > 1"
          :model-value="activeTarget"
          mandatory
          density="compact"
          color="primary"
          divided
          @update:model-value="emit('select-target', $event)"
        >
          <v-btn
            v-for="item in targetItems"
            :key="item.value"
            :value="item.value"
            size="small"
          >
            {{ item.title }}
          </v-btn>
        </v-btn-toggle>
      </div>

      <PreviewPanel
        :build="activeBuild"
        :plugin="plugin"
      />
    </v-card>
  </div>
</template>
