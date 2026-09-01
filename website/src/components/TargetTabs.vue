<script setup>
import { computed } from "vue";
import { sendableBuilds, targetName } from "../utils/plugin.js";

const props = defineProps({
  plugin: {
    type: Object,
    required: true,
  },
  activeTarget: {
    type: String,
    required: true,
  },
});

const emit = defineEmits(["select-target"]);

const builds = computed(() => sendableBuilds(props.plugin));
const showTabs = computed(() => builds.value.length > 1);
</script>

<template>
  <div
    v-if="showTabs"
    class="target-tabs"
    role="tablist"
  >
    <button
      v-for="build in builds"
      :key="build.target"
      type="button"
      role="tab"
      class="target-tabs__item"
      :class="{ 'is-active': build.target === activeTarget }"
      :aria-selected="build.target === activeTarget ? 'true' : 'false'"
      @click="emit('select-target', build.target)"
    >
      {{ targetName(build.target) }}
    </button>
  </div>
</template>
