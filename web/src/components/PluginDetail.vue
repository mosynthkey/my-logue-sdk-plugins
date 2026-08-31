<script setup>
import { computed } from "vue";
import PreviewPanel from "./PreviewPanel.vue";
import SendButton from "./SendButton.vue";
import TargetTabs from "./TargetTabs.vue";
import { SENDABLE_TARGETS } from "../constants.js";
import { buildForTarget, sendableBuilds } from "../utils/plugin.js";

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

const emit = defineEmits(["select-target", "send"]);

const activeBuild = computed(() => buildForTarget(props.plugin, props.activeTarget));

const canSend = computed(() => {
  const builds = sendableBuilds(props.plugin);
  return builds.some((build) => build.target === props.activeTarget && SENDABLE_TARGETS.has(build.target));
});
</script>

<template>
  <main class="detail">
    <header class="detail__head">
      <h2 class="detail__name">{{ plugin.name }}</h2>
      <p class="detail__desc">{{ plugin.description }}</p>
      <TargetTabs
        :plugin="plugin"
        :active-target="activeTarget"
        @select-target="emit('select-target', $event)"
      />
    </header>

    <PreviewPanel
      :build="activeBuild"
      :plugin="plugin"
    />

    <footer class="detail__foot">
      <div class="send-actions">
        <SendButton
          v-if="canSend"
          :plugin="plugin"
          :target="activeTarget"
          @send="(pluginItem, target) => emit('send', pluginItem, target)"
        />
      </div>
    </footer>
  </main>
</template>
