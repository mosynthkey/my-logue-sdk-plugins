<script setup>
import { onMounted, onUnmounted, watch } from "vue";
import AudioUnlockOverlay from "./components/AudioUnlockOverlay.vue";
import PluginDetail from "./components/PluginDetail.vue";
import PluginSidebar from "./components/PluginSidebar.vue";
import SendModal from "./components/SendModal.vue";
import { useCatalog } from "./composables/useCatalog.js";
import { useMidiSend } from "./composables/useMidiSend.js";
import { usePluginSelection } from "./composables/usePluginSelection.js";

const { catalog, loadError, loading } = useCatalog();
const {
  selectedPluginId,
  activePlugin,
  activeTarget,
  selectPlugin,
  selectTarget,
  initializeSelection,
} = usePluginSelection(catalog);

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
    <AudioUnlockOverlay />

    <PluginSidebar
      v-if="catalog"
      :plugins="catalog.plugins"
      :selected-plugin-id="selectedPluginId"
      @select-plugin="selectPlugin"
    />

    <PluginDetail
      v-if="activePlugin"
      :plugin="activePlugin"
      :active-target="activeTarget"
      @select-target="(target) => selectTarget(activePlugin.id, target)"
      @send="openSendModal"
    />

    <div
      v-else-if="loading"
      class="detail detail--empty"
    >
      <p>Loading plugins…</p>
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
