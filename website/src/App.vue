<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import PluginDetail from "./components/PluginDetail.vue";
import PluginSidebar from "./components/PluginSidebar.vue";
import SendModal from "./components/SendModal.vue";
import { useCatalog } from "./composables/useCatalog.js";
import { provideI18n } from "./composables/useI18n.js";
import { useMidiSend } from "./composables/useMidiSend.js";
import { usePluginSelection } from "./composables/usePluginSelection.js";
import { useSiteQuery } from "./composables/useSiteQuery.js";
import { downloadVisibleUnitsZip, unitZipEntries } from "./utils/downloadUnitsZip.js";

const { catalog, loadError, loading } = useCatalog();
const { t } = provideI18n();
const siteQuery = useSiteQuery();
const {
  selectedPluginId,
  activePlugin,
  activeTarget,
  sidebarPlugins,
  selectPlugin,
  selectTarget,
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
  outputPorts,
  inputPorts,
  selectedOutputId,
  selectedInputId,
  channel,
  slot,
  slotLabel,
  midiHint,
  slotOptions,
  openSendModal,
  closeSendModal,
  sendPlugin,
  onMidiSettingChange,
} = useMidiSend();

const zipBusy = ref(false);
const zipError = ref("");
const zipDisabled = computed(() => unitZipEntries(sidebarPlugins.value).length === 0);

async function onDownloadAllUnits() {
  if (zipBusy.value || zipDisabled.value) {
    return;
  }
  zipBusy.value = true;
  zipError.value = "";
  try {
    await downloadVisibleUnitsZip(sidebarPlugins.value);
  } catch {
    zipError.value = t("downloadAllError");
  } finally {
    zipBusy.value = false;
  }
}

watch(catalog, (nextCatalog) => {
  if (nextCatalog?.plugins?.length) {
    initializeSelection();
  }
});

function onKeyDown(event) {
  if (event.key === "Escape" && isOpen.value) {
    closeSendModal();
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
      :download-all-busy="zipBusy"
      :download-all-disabled="zipDisabled"
      :download-all-error="zipError"
      @select-plugin="selectPlugin"
      @download-all="onDownloadAllUnits"
    />

    <PluginDetail
      v-if="activePlugin"
      :plugin="activePlugin"
      :active-target="activeTarget"
      :download-all-busy="zipBusy"
      :download-all-disabled="zipDisabled"
      @select-target="(target) => selectTarget(activePlugin.id, target)"
      @send="openSendModal"
      @download-all="onDownloadAllUnits"
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
      :output-ports="outputPorts"
      :input-ports="inputPorts"
      :selected-output-id="selectedOutputId"
      :selected-input-id="selectedInputId"
      :channel="channel"
      :slot="slot"
      :slot-label="slotLabel"
      :midi-hint="midiHint"
      :slot-options="slotOptions"
      @close="closeSendModal"
      @send="sendPlugin"
      @update:selected-output-id="selectedOutputId = $event"
      @update:selected-input-id="selectedInputId = $event"
      @update:channel="channel = $event"
      @update:slot="slot = $event"
      @midi-setting-change="onMidiSettingChange"
    />
  </div>
</template>
