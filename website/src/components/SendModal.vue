<script setup>
import { computed } from "vue";
import { targetName } from "../utils/plugin.js";
import { useI18n } from "../composables/useI18n.js";

const props = defineProps({
  isOpen: {
    type: Boolean,
    required: true,
  },
  plugin: {
    type: Object,
    default: null,
  },
  target: {
    type: String,
    default: "nts-1_mkii",
  },
  webMidiSupported: {
    type: Boolean,
    required: true,
  },
  logLines: {
    type: Array,
    required: true,
  },
  deviceStatusText: {
    type: String,
    required: true,
  },
  deviceStatusKind: {
    type: String,
    required: true,
  },
  sendDisabled: {
    type: Boolean,
    required: true,
  },
  outputPorts: {
    type: Array,
    required: true,
  },
  inputPorts: {
    type: Array,
    required: true,
  },
  selectedOutputId: {
    type: String,
    required: true,
  },
  selectedInputId: {
    type: String,
    required: true,
  },
  channel: {
    type: Number,
    required: true,
  },
  slot: {
    type: Number,
    required: true,
  },
  slotLabel: {
    type: String,
    required: true,
  },
  midiHint: {
    type: String,
    required: true,
  },
  slotOptions: {
    type: Array,
    required: true,
  },
});

const emit = defineEmits([
  "close",
  "send",
  "update:selectedOutputId",
  "update:selectedInputId",
  "update:channel",
  "update:slot",
  "midi-setting-change",
]);
const { t } = useI18n();

const modalTitle = computed(() => props.plugin?.name || "Plugin");
const modalKicker = computed(() => t("sendTo", { target: targetName(props.target) }));
</script>

<template>
  <div
    v-if="isOpen"
    id="send-modal"
    class="modal"
  >
    <div class="modal__backdrop" @click="emit('close')" />
    <div
      class="modal__dialog"
      role="dialog"
      aria-modal="true"
      aria-labelledby="send-modal-title"
    >
      <header class="modal__header">
        <div>
          <p class="modal__kicker" id="send-modal-kicker">{{ modalKicker }}</p>
          <h2 id="send-modal-title">{{ modalTitle }}</h2>
        </div>
        <button
          type="button"
          class="modal__close"
          :aria-label="t('close')"
          @click="emit('close')"
        >
          ×
        </button>
      </header>

      <div
        v-if="!webMidiSupported"
        id="midi-unsupported"
        class="modal__notice"
      >
        <p>{{ t("midiRequired") }}</p>
      </div>

      <div v-else id="midi-panel">
        <p
          id="device-status"
          class="status"
          :data-kind="deviceStatusKind"
        >
          {{ deviceStatusText }}
        </p>

        <label class="field">
          <span>{{ t("output") }}</span>
          <select
            id="midi-output"
            :value="selectedOutputId"
            @change="emit('update:selectedOutputId', $event.target.value); emit('midi-setting-change')"
          >
            <option
              v-if="outputPorts.length === 0"
              value=""
            >
              {{ t("noPorts") }}
            </option>
            <option
              v-for="port in outputPorts"
              :key="port.id"
              :value="port.id"
            >
              {{ port.label }}
            </option>
          </select>
        </label>

        <label class="field">
          <span>{{ t("input") }}</span>
          <select
            id="midi-input"
            :value="selectedInputId"
            @change="emit('update:selectedInputId', $event.target.value); emit('midi-setting-change')"
          >
            <option
              v-if="inputPorts.length === 0"
              value=""
            >
              {{ t("noPorts") }}
            </option>
            <option
              v-for="port in inputPorts"
              :key="port.id"
              :value="port.id"
            >
              {{ port.label }}
            </option>
          </select>
        </label>

        <div class="field-row">
          <label class="field">
            <span>{{ t("channel") }}</span>
            <input
              id="channel"
              type="number"
              min="1"
              max="16"
              :value="channel"
              @change="emit('update:channel', Number($event.target.value)); emit('midi-setting-change')"
            >
          </label>
          <label class="field">
            <span id="slot-label">{{ slotLabel }}</span>
            <select
              id="slot"
              :value="slot"
              @change="emit('update:slot', Number($event.target.value))"
            >
              <option
                v-for="option in slotOptions"
                :key="option.value"
                :value="option.value"
              >
                {{ option.label }}
              </option>
            </select>
          </label>
        </div>

        <p class="hint" id="midi-hint">{{ midiHint }}</p>

        <div class="modal__actions">
          <button
            type="button"
            class="button button-secondary"
            @click="emit('close')"
          >
            {{ t("cancel") }}
          </button>
          <button
            type="button"
            class="button button-primary"
            id="send"
            :disabled="sendDisabled"
            @click="emit('send')"
          >
            {{ t("sendToSlot") }}
          </button>
        </div>
      </div>

      <div class="log-wrap">
        <h3>{{ t("log") }}</h3>
        <div id="log" class="log" role="log" aria-live="polite">
          <p
            v-for="(line, lineIndex) in logLines"
            :key="lineIndex"
            class="log-line"
            :class="`log-${line.kind}`"
          >
            {{ line.message }}
          </p>
        </div>
      </div>
    </div>
  </div>
</template>
