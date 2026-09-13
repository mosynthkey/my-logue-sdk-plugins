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
  statusText: {
    type: String,
    required: true,
  },
  statusKind: {
    type: String,
    required: true,
  },
  logLines: {
    type: Array,
    required: true,
  },
  sending: {
    type: Boolean,
    required: true,
  },
  showChrome152Hint: {
    type: Boolean,
    default: false,
  },
});

const emit = defineEmits(["close"]);
const { t } = useI18n();

const modalTitle = computed(() => props.plugin?.name || "Plugin");
const statusColor = computed(() => {
  if (props.statusKind === "ok") return "success";
  if (props.statusKind === "error") return "error";
  if (props.statusKind === "warn") return "warning";
  if (props.statusKind === "busy") return "info";
  return "secondary";
});
</script>

<template>
  <v-dialog
    :model-value="isOpen"
    max-width="520"
    :persistent="sending"
    @update:model-value="(value) => !value && !sending && emit('close')"
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
          :disabled="sending"
          @click="emit('close')"
        />
      </v-card-title>

      <v-card-text>
        <v-alert
          :color="statusColor"
          variant="tonal"
          class="mb-4"
          :text="statusText"
        />

        <v-alert
          v-if="showChrome152Hint && statusKind === 'error'"
          type="warning"
          variant="tonal"
          class="mb-4"
          :text="t('chrome152Hint')"
        />

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
          :disabled="sending"
          @click="emit('close')"
        >
          {{ t("close") }}
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>
