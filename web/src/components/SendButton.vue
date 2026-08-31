<script setup>
import { sendLabel } from "../utils/plugin.js";
import { SEND_ICON } from "../constants.js";

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
    <span class="send-button__icon" v-html="SEND_ICON" />
    {{ sendLabel(target) }}
  </button>
</template>
