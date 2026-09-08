<script setup>
import { computed } from "vue";
import PreviewPanel from "./PreviewPanel.vue";
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

const emit = defineEmits(["select-target", "send", "explain-dsp"]);
const { pluginDescription, t } = useI18n();

const activeBuild = computed(() => buildForTarget(props.plugin, props.activeTarget));
const downloads = computed(() => downloadableBuilds(props.plugin));
const sends = computed(() => sendableBuilds(props.plugin));

const targetItems = computed(() => {
  const targets = new Set([
    ...downloads.value.map((build) => build.target),
    ...sends.value.map((build) => build.target),
    ...(props.plugin.targets || []),
  ]);
  if (props.activeTarget) {
    targets.add(props.activeTarget);
  }
  return [...targets].map((target) => ({
    value: target,
    title: targetName(target),
  }));
});
</script>

<template>
  <div class="d-flex flex-column ga-6">
    <div>
      <h1 class="text-h4 font-weight-bold mb-2">{{ plugin.name }}</h1>
      <p class="text-body-large text-medium-emphasis mb-4">
        {{ pluginDescription(plugin) }}
      </p>
      <v-btn
        variant="tonal"
        prepend-icon="mdi-sitemap"
        @click="emit('explain-dsp', plugin)"
      >
        {{ t("dspHowItWorks") }}
      </v-btn>
    </div>

    <v-row v-if="downloads.length || sends.length">
      <v-col
        v-if="downloads.length"
        cols="12"
        md="6"
      >
        <h2 class="text-title-medium mb-3">{{ t("download") }}</h2>
        <div class="d-flex flex-wrap ga-2">
          <v-btn
            v-for="build in downloads"
            :key="`download-${build.target}`"
            :href="build.file"
            :download="unitFileName(build)"
            variant="outlined"
            prepend-icon="mdi-download"
          >
            {{ targetName(build.target) }}
          </v-btn>
        </div>
      </v-col>

      <v-col
        v-if="sends.length"
        cols="12"
        md="6"
      >
        <h2 class="text-title-medium mb-3">{{ t("sendToDevice") }}</h2>
        <div class="d-flex flex-wrap ga-2">
          <v-btn
            v-for="build in sends"
            :key="`send-${build.target}`"
            variant="outlined"
            prepend-icon="mdi-usb"
            @click="emit('send', plugin, build.target)"
          >
            {{ targetName(build.target) }}
          </v-btn>
        </div>
      </v-col>
    </v-row>

    <v-card
      variant="flat"
      class="pa-4"
    >
      <div class="d-flex flex-wrap align-center justify-space-between ga-3 mb-4">
        <h2 class="text-title-medium">{{ t("preview") }}</h2>
        <v-btn-toggle
          v-if="targetItems.length > 1"
          :model-value="activeTarget"
          mandatory
          density="compact"
          color="primary"
          divided
          @update:model-value="emit('select-target', $event)"
        >
          <v-btn
            v-for="item in targetItems"
            :key="item.value"
            :value="item.value"
            size="small"
          >
            {{ item.title }}
          </v-btn>
        </v-btn-toggle>
      </div>

      <PreviewPanel
        :build="activeBuild"
        :plugin="plugin"
      />
    </v-card>
  </div>
</template>
