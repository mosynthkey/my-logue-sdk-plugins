<script setup>
import { computed } from "vue";
import PreviewPanel from "./PreviewPanel.vue";
import TargetTabs from "./TargetTabs.vue";
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
const sends = computed(() => sendableBuilds(props.plugin));
</script>

<template>
  <main class="detail">
    <header class="detail__head">
      <h2 class="detail__name">{{ plugin.name }}</h2>
      <p class="detail__desc">{{ pluginDescription(plugin) }}</p>
    </header>

    <section
      v-if="downloads.length || sends.length"
      class="detail__section detail__section--devices"
    >
      <div
        v-if="downloads.length"
        class="detail__device-group"
        :aria-label="t('download')"
      >
        <h3 class="detail__section-title">{{ t("download") }}</h3>
        <div class="detail__device-buttons">
          <a
            v-for="build in downloads"
            :key="`download-${build.target}`"
            class="device-button"
            :href="build.file"
            :download="unitFileName(build)"
            :aria-label="t('downloadFor', { target: targetName(build.target) })"
            :title="t('downloadFor', { target: targetName(build.target) })"
          >
            <svg
              class="device-button__icon"
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
      </div>

      <div
        v-if="sends.length"
        class="detail__device-group"
        :aria-label="t('sendToDevice')"
      >
        <h3 class="detail__section-title">{{ t("sendToDevice") }}</h3>
        <div class="detail__device-buttons">
          <button
            v-for="build in sends"
            :key="`send-${build.target}`"
            type="button"
            class="device-button"
            :aria-label="t('sendTo', { target: targetName(build.target) })"
            :title="t('sendTo', { target: targetName(build.target) })"
            @click="emit('send', plugin, build.target)"
          >
            <svg
              class="device-button__icon"
              viewBox="0 0 24 24"
              aria-hidden="true"
            >
              <path d="M12 3v12" />
              <path d="m8 11 4 4 4-4" />
              <rect x="4" y="17" width="16" height="4" rx="1" />
            </svg>
            <span>{{ targetName(build.target) }}</span>
          </button>
        </div>
      </div>
    </section>

    <section
      class="detail__section detail__section--preview"
      :aria-label="t('preview')"
    >
      <div class="detail__section-head">
        <h3 class="detail__section-title">{{ t("preview") }}</h3>
        <div class="detail__actions">
          <TargetTabs
            :plugin="plugin"
            :active-target="activeTarget"
            @select-target="emit('select-target', $event)"
          />
        </div>
      </div>

      <PreviewPanel
        :build="activeBuild"
        :plugin="plugin"
      />
    </section>
  </main>
</template>
