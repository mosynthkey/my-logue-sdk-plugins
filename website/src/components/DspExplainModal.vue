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
  <div
    v-if="isOpen"
    id="dsp-explain-modal"
    class="modal"
  >
    <div
      class="modal__backdrop"
      @click="emit('close')"
    />
    <div
      class="modal__dialog modal__dialog--wide"
      role="dialog"
      aria-modal="true"
      aria-labelledby="dsp-explain-modal-title"
    >
      <header class="modal__header">
        <div>
          <p class="modal__kicker">{{ t("dspHowItWorks") }}</p>
          <h2 id="dsp-explain-modal-title">{{ modalTitle }}</h2>
        </div>
        <button
          type="button"
          class="modal__close"
          :aria-label="t('close')"
          @click="emit('close')"
        >
          ×
        </button>
      </header>

      <div
        v-if="explain"
        class="dsp-explain"
      >
        <p class="dsp-explain__body">{{ bodyText }}</p>
        <h3 class="dsp-explain__diagram-title">{{ t("dspBlockDiagram") }}</h3>
        <MermaidDiagram
          :key="plugin?.id"
          :diagram-id="plugin?.id || 'plugin'"
          :chart="explain.mermaid"
        />
      </div>

      <div
        v-else
        class="modal__notice"
      >
        <p>{{ t("dspExplainMissing") }}</p>
      </div>

      <div class="modal__actions">
        <button
          type="button"
          class="button button-secondary"
          @click="emit('close')"
        >
          {{ t("close") }}
        </button>
      </div>
    </div>
  </div>
</template>
