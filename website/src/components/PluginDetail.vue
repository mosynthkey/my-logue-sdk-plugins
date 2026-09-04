<script setup>
import { computed } from "vue";
import PreviewPanel from "./PreviewPanel.vue";
import SendButton from "./SendButton.vue";
import TargetTabs from "./TargetTabs.vue";
import { SENDABLE_TARGETS } from "../constants.js";
import { useI18n } from "../composables/useI18n.js";
import {
  buildForTarget,
  downloadableBuilds,
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
});

const emit = defineEmits(["select-target", "send"]);
const { pluginDescription, t } = useI18n();

const activeBuild = computed(() => buildForTarget(props.plugin, props.activeTarget));
const downloads = computed(() => downloadableBuilds(props.plugin));

const canSend = computed(() => {
  const builds = sendableBuilds(props.plugin);
  return builds.some((build) => build.target === props.activeTarget && SENDABLE_TARGETS.has(build.target));
});
</script>

<template>
  <main class="detail">
    <header class="detail__head">
      <h2 class="detail__name">{{ plugin.name }}</h2>
      <p class="detail__desc">{{ pluginDescription(plugin) }}</p>

      <div
        v-if="downloads.length"
        class="detail__downloads"
      >
        <a
          v-for="build in downloads"
          :key="build.target"
          class="download-button"
          :href="build.file"
          :download="unitFileName(build)"
          :aria-label="t('downloadFor', { target: targetName(build.target) })"
          :title="t('downloadFor', { target: targetName(build.target) })"
        >
          <svg
            class="download-button__icon"
            viewBox="0 0 24 24"
            aria-hidden="true"
          >
            <path d="M12 3v12" />
            <path d="m7 11 5 5 5-5" />
            <path d="M5 19h14" />
          </svg>
          <span>{{ targetName(build.target) }}</span>
        </a>
      </div>

      <div class="detail__actions">
        <TargetTabs
          :plugin="plugin"
          :active-target="activeTarget"
          @select-target="emit('select-target', $event)"
        />
        <SendButton
          v-if="canSend"
          :plugin="plugin"
          :target="activeTarget"
          @send="(pluginItem, target) => emit('send', pluginItem, target)"
        />
      </div>
    </header>

    <PreviewPanel
      :build="activeBuild"
      :plugin="plugin"
    />
  </main>
</template>
