<script setup>
import { onMounted, onUnmounted, ref, watch } from "vue";
import DspExplainModal from "./components/DspExplainModal.vue";
import PluginDetail from "./components/PluginDetail.vue";
import PluginSidebar from "./components/PluginSidebar.vue";
import ProgramEditorModal from "./components/ProgramEditorModal.vue";
import SendModal from "./components/SendModal.vue";
import { useCatalog } from "./composables/useCatalog.js";
import { provideI18n } from "./composables/useI18n.js";
import { useMidiSend } from "./composables/useMidiSend.js";
import { usePluginSelection } from "./composables/usePluginSelection.js";
import { useProgramEditor } from "./composables/useProgramEditor.js";
import { useSiteQuery } from "./composables/useSiteQuery.js";

const { catalog, loadError, loading } = useCatalog();
const { t } = provideI18n();
const siteQuery = useSiteQuery();
const {
  selectedPluginId,
  selectedCategory,
  activePlugin,
  activeTarget,
  sidebarPlugins,
  selectPlugin,
  selectTarget,
  selectCategory,
  initializeSelection,
} = usePluginSelection(catalog, siteQuery);

const {
  isOpen,
  pendingPlugin,
  pendingTarget,
  webMidiSupported,
  logLines,
  deviceStatusText,
  deviceStatusKind,
  sendDisabled,
  selectedOutputLabel,
  selectedInputLabel,
  channel,
  slot,
  slotLabel,
  slotOptions,
  openSendModal,
  closeSendModal,
  sendPlugin,
  sendToSlot,
  nts3Connected,
  connectedTargets,
  inlineSlotsByTarget,
  inlineSlotsLoading,
  inlineStatusText,
  inlineStatusKind,
  sending,
  syncInlineSlots,
  startPresenceWatch,
} = useMidiSend();

const {
  isOpen: programEditorOpen,
  program,
  openEditor,
  closeEditor,
  selectSlot,
  updateParam,
  clearActiveSlot,
  clampParamRange,
} = useProgramEditor();

const dspExplainOpen = ref(false);
const dspExplainPlugin = ref(null);
const drawer = ref(true);

function openDspExplainModal(plugin) {
  dspExplainPlugin.value = plugin;
  dspExplainOpen.value = true;
}

function closeDspExplainModal() {
  dspExplainOpen.value = false;
  dspExplainPlugin.value = null;
}

function openProgramEditor() {
  if (!nts3Connected.value) {
    return;
  }
  openEditor();
}

function closeProgramEditor() {
  closeEditor();
}

function receiveProgram() {
  // Placeholder: Current Program Data Dump Request will go here.
}

function updateActiveSlot(patch) {
  const active = program.slots[program.activeSlot];
  Object.assign(active, patch);
  if ("releaseTime" in patch) {
    active.releaseTime = clampParamRange(active.releaseTime);
  }
  if ("outGain" in patch) {
    active.outGain = clampParamRange(active.outGain);
  }
  if ("depth" in patch) {
    active.depth = clampParamRange(active.depth);
  }
  if ("x" in patch) {
    active.x = clampParamRange(active.x);
  }
  if ("y" in patch) {
    active.y = clampParamRange(active.y);
  }
}

watch(catalog, (nextCatalog) => {
  if (nextCatalog?.plugins?.length) {
    initializeSelection();
  }
});

watch(nts3Connected, (connected) => {
  if (!connected && programEditorOpen.value) {
    closeProgramEditor();
  }
});

watch(activePlugin, (plugin) => {
  syncInlineSlots(plugin);
});

watch(connectedTargets, () => {
  syncInlineSlots(activePlugin.value);
}, { deep: true });

function onKeyDown(event) {
  if (event.key !== "Escape") return;
  if (programEditorOpen.value) {
    closeProgramEditor();
    return;
  }
  if (dspExplainOpen.value) {
    closeDspExplainModal();
    return;
  }
  if (isOpen.value) {
    closeSendModal();
  }
}

onMounted(async () => {
  document.addEventListener("keydown", onKeyDown);
  await startPresenceWatch();
  if (activePlugin.value) {
    syncInlineSlots(activePlugin.value);
  }
});

onUnmounted(() => {
  document.removeEventListener("keydown", onKeyDown);
});
</script>

<template>
  <v-app>
    <v-app-bar
      flat
      border
      density="comfortable"
    >
      <v-app-bar-nav-icon
        class="d-md-none"
        @click="drawer = !drawer"
      />
      <v-app-bar-title class="text-uppercase font-weight-bold">
        My Logue SDK Plugins
      </v-app-bar-title>
      <v-spacer />
      <v-btn
        v-if="nts3Connected"
        color="primary"
        variant="flat"
        prepend-icon="mdi-tune-vertical"
        :active="programEditorOpen"
        @click="openProgramEditor"
      >
        NTS-3 Program Editor
      </v-btn>
    </v-app-bar>

    <PluginSidebar
      v-if="catalog"
      v-model="drawer"
      :plugins="sidebarPlugins"
      :selected-plugin-id="selectedPluginId"
      :selected-category="selectedCategory"
      @select-plugin="selectPlugin"
      @select-category="selectCategory"
    />

    <v-main>
      <v-container
        fluid
        class="pa-4 pa-md-6"
      >
        <PluginDetail
          v-if="activePlugin"
          :plugin="activePlugin"
          :active-target="activeTarget"
          :connected-targets="connectedTargets"
          :inline-slots-by-target="inlineSlotsByTarget"
          :inline-slots-loading="inlineSlotsLoading"
          :inline-status-text="inlineStatusText"
          :inline-status-kind="inlineStatusKind"
          :sending="sending"
          @select-target="(target) => selectTarget(activePlugin.id, target)"
          @send="openSendModal"
          @send-slot="sendToSlot"
          @explain-dsp="openDspExplainModal"
        />

        <v-alert
          v-else-if="loading"
          type="info"
          variant="tonal"
          :text="t('loading')"
        />

        <v-alert
          v-else-if="loadError"
          type="error"
          variant="tonal"
          :text="loadError"
        />
      </v-container>
    </v-main>

    <SendModal
      :is-open="isOpen"
      :plugin="pendingPlugin"
      :target="pendingTarget"
      :web-midi-supported="webMidiSupported"
      :log-lines="logLines"
      :device-status-text="deviceStatusText"
      :device-status-kind="deviceStatusKind"
      :send-disabled="sendDisabled"
      :selected-output-label="selectedOutputLabel"
      :selected-input-label="selectedInputLabel"
      :channel="channel"
      :slot="slot"
      :slot-label="slotLabel"
      :slot-options="slotOptions"
      @close="closeSendModal"
      @send="sendPlugin"
      @update:slot="slot = $event"
    />

    <DspExplainModal
      :is-open="dspExplainOpen"
      :plugin="dspExplainPlugin"
      @close="closeDspExplainModal"
    />

    <ProgramEditorModal
      :is-open="programEditorOpen"
      :program="program"
      @close="closeProgramEditor"
      @receive="receiveProgram"
      @select-slot="selectSlot"
      @update:routing="program.routing = $event"
      @clear-slot="clearActiveSlot"
      @update-slot="updateActiveSlot"
      @update-param="updateParam"
    />
  </v-app>
</template>
