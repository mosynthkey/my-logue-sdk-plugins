<script setup>
import { onMounted, onUnmounted, ref, watch } from "vue";
import DspExplainModal from "./components/DspExplainModal.vue";
import PluginDetail from "./components/PluginDetail.vue";
import PluginSidebar from "./components/PluginSidebar.vue";
import SendModal from "./components/SendModal.vue";
import { useCatalog } from "./composables/useCatalog.js";
import { provideI18n } from "./composables/useI18n.js";
import { useMidiSend } from "./composables/useMidiSend.js";
import { usePluginSelection } from "./composables/usePluginSelection.js";
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
} = useMidiSend();

const dspExplainOpen = ref(false);
const dspExplainPlugin = ref(null);

function openDspExplainModal(plugin) {
  dspExplainPlugin.value = plugin;
  dspExplainOpen.value = true;
  document.body.classList.add("modal-open");
}

function closeDspExplainModal() {
  dspExplainOpen.value = false;
  dspExplainPlugin.value = null;
  if (!isOpen.value) {
    document.body.classList.remove("modal-open");
  }
}

function handleCloseSendModal() {
  closeSendModal();
  if (dspExplainOpen.value) {
    document.body.classList.add("modal-open");
  }
}

watch(catalog, (nextCatalog) => {
  if (nextCatalog?.plugins?.length) {
    initializeSelection();
  }
});

function onKeyDown(event) {
  if (event.key !== "Escape") return;
  if (dspExplainOpen.value) {
    closeDspExplainModal();
    return;
  }
  if (isOpen.value) {
    handleCloseSendModal();
  }
}

onMounted(() => {
  document.addEventListener("keydown", onKeyDown);
});

onUnmounted(() => {
  document.removeEventListener("keydown", onKeyDown);
});
</script>

<template>
  <div class="app">
    <PluginSidebar
      v-if="catalog"
      :plugins="sidebarPlugins"
      :selected-plugin-id="selectedPluginId"
      :selected-category="selectedCategory"
      @select-plugin="selectPlugin"
      @select-category="selectCategory"
    />

    <PluginDetail
      v-if="activePlugin"
      :plugin="activePlugin"
      :active-target="activeTarget"
      @select-target="(target) => selectTarget(activePlugin.id, target)"
      @send="openSendModal"
      @explain-dsp="openDspExplainModal"
    />

    <div
      v-else-if="loading"
      class="detail detail--empty"
    >
      <p>{{ t("loading") }}</p>
    </div>

    <div
      v-else-if="loadError"
      class="detail detail--empty"
    >
      <p class="empty">{{ loadError }}</p>
    </div>

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
      @close="handleCloseSendModal"
      @send="sendPlugin"
      @update:slot="slot = $event"
    />

    <DspExplainModal
      :is-open="dspExplainOpen"
      :plugin="dspExplainPlugin"
      @close="closeDspExplainModal"
    />
  </div>
</template>
