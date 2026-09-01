<script setup>
import { targetName } from "../utils/plugin.js";
import { useI18n } from "../composables/useI18n.js";

defineProps({
  plugin: {
    type: Object,
    required: true,
  },
  target: {
    type: String,
    required: true,
  },
});

const emit = defineEmits(["send"]);
const { t } = useI18n();
</script>

<template>
  <button
    type="button"
    class="send-button"
    @click="emit('send', plugin, target)"
  >
    {{ t("sendTo", { target: targetName(target) }) }}
  </button>
</template>
