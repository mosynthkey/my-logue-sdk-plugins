<script setup>
import { sendLabel } from "../utils/plugin.js";

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
</script>

<template>
  <button
    type="button"
    class="send-button"
    @click="emit('send', plugin, target)"
  >
    {{ sendLabel(target) }}
  </button>
</template>
