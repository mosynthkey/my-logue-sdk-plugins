<script setup>
import { computed } from "vue";
import { targetName } from "../utils/plugin.js";
import { useI18n } from "../composables/useI18n.js";

const props = defineProps({
  isOpen: {
    type: Boolean,
    required: true,
  },
  plugin: {
    type: Object,
    default: null,
  },
  target: {
    type: String,
    default: "nts-1_mkii",
  },
  webMidiSupported: {
    type: Boolean,
    required: true,
  },
  logLines: {
    type: Array,
    required: true,
  },
  deviceStatusText: {
    type: String,
    required: true,
  },
  deviceStatusKind: {
    type: String,
    required: true,
  },
  sendDisabled: {
    type: Boolean,
    required: true,
  },
  selectedOutputLabel: {
    type: String,
    required: true,
  },
  selectedInputLabel: {
    type: String,
    required: true,
  },
  channel: {
    type: Number,
    required: true,
  },
  slot: {
    type: Number,
    required: true,
  },
  slotLabel: {
    type: String,
    required: true,
  },
  slotOptions: {
    type: Array,
    required: true,
  },
});

const emit = defineEmits(["close", "send", "update:slot"]);
const { t } = useI18n();

const modalTitle = computed(() => props.plugin?.name || "Plugin");
const statusColor = computed(() => {
  if (props.deviceStatusKind === "ok") return "success";
  if (props.deviceStatusKind === "error") return "error";
  if (props.deviceStatusKind === "warn") return "warning";
  if (props.deviceStatusKind === "busy") return "info";
  return "secondary";
});
</script>

<template>
  <v-dialog
    :model-value="isOpen"
    max-width="560"
    @update:model-value="(value) => !value && emit('close')"
  >
    <v-card>
      <v-card-title class="d-flex align-center justify-space-between">
        <div>
          <div class="text-label-large text-medium-emphasis">
            {{ t("sendTo", { target: targetName(target) }) }}
          </div>
          <div class="text-title-large">{{ modalTitle }}</div>
        </div>
        <v-btn
          icon="mdi-close"
          variant="text"
          :aria-label="t('close')"
          @click="emit('close')"
        />
      </v-card-title>

      <v-card-text>
        <v-alert
          v-if="!webMidiSupported"
          type="warning"
          variant="tonal"
          class="mb-4"
          :text="t('midiRequired')"
        />

        <template v-else>
          <p class="text-body-medium text-medium-emphasis mb-4">
            {{ t("connectUsbHint", { target: targetName(target) }) }}
          </p>

          <v-alert
            :color="statusColor"
            variant="tonal"
            class="mb-4"
            :text="deviceStatusText"
          />

          <v-row dense>
            <v-col cols="6">
              <div class="text-label-medium text-medium-emphasis">{{ t("output") }}</div>
              <div>{{ selectedOutputLabel || t("noPorts") }}</div>
            </v-col>
            <v-col cols="6">
              <div class="text-label-medium text-medium-emphasis">{{ t("input") }}</div>
              <div>{{ selectedInputLabel || t("noPorts") }}</div>
            </v-col>
            <v-col cols="6">
              <div class="text-label-medium text-medium-emphasis">{{ t("channel") }}</div>
              <div>{{ channel }}</div>
            </v-col>
            <v-col cols="6">
              <v-select
                :model-value="slot"
                :items="slotOptions"
                item-title="label"
                item-value="value"
                :label="slotLabel"
                density="compact"
                hide-details
                @update:model-value="emit('update:slot', $event)"
              />
            </v-col>
          </v-row>
        </template>

        <v-divider class="my-4" />

        <div class="text-title-small mb-2">{{ t("log") }}</div>
        <v-sheet
          border
          rounded
          class="pa-3"
          max-height="180"
          style="overflow: auto; font-family: monospace; font-size: 0.75rem;"
        >
          <div
            v-for="(line, lineIndex) in logLines"
            :key="lineIndex"
            :class="{
              'text-success': line.kind === 'ok',
              'text-error': line.kind === 'error',
              'text-warning': line.kind === 'warn',
            }"
          >
            {{ line.message }}
          </div>
        </v-sheet>
      </v-card-text>

      <v-card-actions>
        <v-spacer />
        <v-btn
          variant="text"
          @click="emit('close')"
        >
          {{ t("cancel") }}
        </v-btn>
        <v-btn
          color="primary"
          variant="flat"
          :disabled="sendDisabled || !webMidiSupported"
          @click="emit('send')"
        >
          {{ t("sendToSlot") }}
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>
