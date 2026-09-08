<script setup>
import { computed } from "vue";
import MermaidDiagram from "./MermaidDiagram.vue";
import { useI18n } from "../composables/useI18n.js";
import { dspExplainFor } from "../data/dspExplain.js";

const props = defineProps({
  isOpen: {
    type: Boolean,
    required: true,
  },
  plugin: {
    type: Object,
    default: null,
  },
});

const emit = defineEmits(["close"]);
const { locale, t } = useI18n();

const explain = computed(() => {
  if (!props.plugin) return null;
  return dspExplainFor(props.plugin.id);
});

const bodyText = computed(() => {
  if (!explain.value) return "";
  return locale.value === "ja" ? explain.value.ja : explain.value.en;
});

const modalTitle = computed(() => props.plugin?.name || "Plugin");
</script>

<template>
  <v-dialog
    :model-value="isOpen"
    max-width="760"
    @update:model-value="(value) => !value && emit('close')"
  >
    <v-card>
      <v-card-title class="d-flex align-center justify-space-between">
        <div>
          <div class="text-label-large text-medium-emphasis">{{ t("dspHowItWorks") }}</div>
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
        <template v-if="explain">
          <p class="text-body-large mb-6">{{ bodyText }}</p>
          <h3 class="text-title-small mb-3">{{ t("dspBlockDiagram") }}</h3>
          <MermaidDiagram
            :key="plugin?.id"
            :diagram-id="plugin?.id || 'plugin'"
            :chart="explain.mermaid"
          />
        </template>
        <v-alert
          v-else
          type="info"
          variant="tonal"
          :text="t('dspExplainMissing')"
        />
      </v-card-text>

      <v-card-actions>
        <v-spacer />
        <v-btn
          color="primary"
          variant="flat"
          @click="emit('close')"
        >
          {{ t("close") }}
        </v-btn>
      </v-card-actions>
    </v-card>
  </v-dialog>
</template>
