import { computed, ref } from "vue";
import {
  DEVICES,
  MODULE_SLOTS,
  NTS1_MKII,
  NTS3_KAOSS,
  detectDevice,
  installUnit,
  listMidiPorts,
  looksLikeDevicePort,
  pickPreferredPort,
  portLabel,
  readModuleSlots,
  readSlotStatus,
} from "../../nts1-midi.js";
import { LOAD_HINT } from "../constants.js";
import { moduleFor } from "../utils/plugin.js";

function deviceForTarget(target) {
  return DEVICES[target] || NTS1_MKII;
}

function portSuffix(name) {
  if (looksLikeDevicePort(name, NTS3_KAOSS)) {
    return "  · NTS-3";
  }
  if (looksLikeDevicePort(name, NTS1_MKII)) {
    return "  · NTS-1";
  }
  return "";
}

function slotOptionLabel(slotIndex, status) {
  if (!status) {
    return `Slot ${slotIndex}`;
  }
  if (status.empty) {
    return `Slot ${slotIndex} · empty`;
  }
  return `Slot ${slotIndex} · ${status.name || "occupied"}`;
}

export function useMidiSend() {
  const isOpen = ref(false);
  const pendingPlugin = ref(null);
  const pendingTarget = ref("nts-1_mkii");
  const webMidiSupported = ref(Boolean(navigator.requestMIDIAccess));

  const logLines = ref([]);
  const deviceStatusText = ref("");
  const deviceStatusKind = ref("idle");
  const sendDisabled = ref(true);

  const outputPorts = ref([]);
  const inputPorts = ref([]);
  const selectedOutputId = ref("");
  const selectedInputId = ref("");
  const channel = ref(1);
  const slot = ref(1);
  const slotLabel = ref("osc slot");
  const midiHint = ref("");

  const midiAccess = ref(null);
  let deviceInquiryToken = 0;
  let slotInquiryToken = 0;
  const currentSlotModule = ref("osc");
  const slotStatuses = ref(new Map());
  const slotStatusVersion = ref(0);
  const unitCache = new Map();

  const slotOptions = computed(() => {
    slotStatusVersion.value;
    const count = MODULE_SLOTS[currentSlotModule.value] || 16;
    const options = [];
    for (let slotIndex = 0; slotIndex < count; slotIndex += 1) {
      options.push({
        value: slotIndex,
        label: slotOptionLabel(slotIndex, slotStatuses.value.get(slotIndex)),
      });
    }
    return options;
  });

  function log(message, kind = "info") {
    logLines.value = [{ message, kind }, ...logLines.value];
  }

  function clearLog() {
    logLines.value = [];
  }

  function setDeviceStatus(text, kind = "idle") {
    deviceStatusText.value = text;
    deviceStatusKind.value = kind;
  }

  function formatDeviceStatus(identity, output) {
    const deviceName = identity?.label || deviceForTarget(pendingTarget.value).shortLabel;
    return `${deviceName} on ${portLabel(output)}`;
  }

  function hasMidiPorts() {
    return midiAccess.value
      && midiAccess.value.outputs.size > 0
      && midiAccess.value.inputs.size > 0;
  }

  function selectedPort(portId, ports) {
    return ports.get(portId) || null;
  }

  function refreshPortLists() {
    if (!midiAccess.value) {
      outputPorts.value = [];
      inputPorts.value = [];
      return;
    }

    const preferredDevice = deviceForTarget(pendingTarget.value);
    const preferredOutput = pickPreferredPort(midiAccess.value.outputs, preferredDevice);
    const preferredInput = pickPreferredPort(midiAccess.value.inputs, preferredDevice);

    outputPorts.value = listMidiPorts(midiAccess.value.outputs).map((port) => ({
      id: port.id,
      label: portLabel(port) + portSuffix(port.name),
    }));
    inputPorts.value = listMidiPorts(midiAccess.value.inputs).map((port) => ({
      id: port.id,
      label: portLabel(port) + portSuffix(port.name),
    }));

    if (preferredOutput) {
      selectedOutputId.value = preferredOutput.id;
    } else if (outputPorts.value.length > 0) {
      selectedOutputId.value = outputPorts.value[0].id;
    }

    if (preferredInput) {
      selectedInputId.value = preferredInput.id;
    } else if (inputPorts.value.length > 0) {
      selectedInputId.value = inputPorts.value[0].id;
    }
  }

  function resetSlotStatuses() {
    slotStatuses.value = new Map();
    slotStatusVersion.value += 1;
  }

  function applySlotModule(module) {
    const previous = slot.value;
    if (module !== currentSlotModule.value) {
      resetSlotStatuses();
      currentSlotModule.value = module;
    }

    slotLabel.value = `${module} slot`;
    const maxSlot = (MODULE_SLOTS[module] || 16) - 1;
    const nextSlot = Number.isFinite(previous) ? Math.min(Math.max(previous, 0), maxSlot) : 1;
    slot.value = nextSlot;
  }

  async function connectMidi() {
    if (!navigator.requestMIDIAccess) {
      return false;
    }

    if (midiAccess.value) {
      return true;
    }

    try {
      midiAccess.value = await navigator.requestMIDIAccess({ sysex: true });
    } catch (error) {
      log(String(error), "error");
      return false;
    }

    midiAccess.value.onstatechange = () => {
      refreshPortLists();
      inquireDevice();
    };
    refreshPortLists();
    log("SysEx enabled.");
    return true;
  }

  async function inquireSlotOccupancy(module) {
    const inquiryToken = ++slotInquiryToken;
    if (!midiAccess.value || !hasMidiPorts()) {
      resetSlotStatuses();
      return;
    }

    const output = selectedPort(selectedOutputId.value, midiAccess.value.outputs);
    const input = selectedPort(selectedInputId.value, midiAccess.value.inputs);
    if (!output || !input) {
      return;
    }

    try {
      const slots = await readModuleSlots(output, input, {
        module,
        channel: channel.value,
        device: deviceForTarget(pendingTarget.value),
      });
      if (inquiryToken !== slotInquiryToken) {
        return;
      }
      const nextStatuses = new Map();
      for (const status of slots) {
        nextStatuses.set(status.slot, status);
      }
      slotStatuses.value = nextStatuses;
      slotStatusVersion.value += 1;
    } catch (error) {
      if (inquiryToken !== slotInquiryToken) {
        return;
      }
      log(`Slot occupancy inquiry failed: ${error.message}`, "warn");
    }
  }

  async function inquireDevice() {
    const inquiryToken = ++deviceInquiryToken;

    if (!navigator.requestMIDIAccess) {
      setDeviceStatus("Use Chrome or Edge for MIDI", "warn");
      sendDisabled.value = true;
      return;
    }

    if (!midiAccess.value) {
      setDeviceStatus(`Looking for ${deviceForTarget(pendingTarget.value).shortLabel}…`, "busy");
      sendDisabled.value = true;
      const connected = await connectMidi();
      if (!connected || inquiryToken !== deviceInquiryToken) {
        return;
      }
    }

    if (!hasMidiPorts()) {
      setDeviceStatus(
        `No MIDI device found. Connect ${deviceForTarget(pendingTarget.value).shortLabel} over USB-C.`,
        "error",
      );
      sendDisabled.value = true;
      return;
    }

    const output = selectedPort(selectedOutputId.value, midiAccess.value.outputs);
    const input = selectedPort(selectedInputId.value, midiAccess.value.inputs);
    if (!output || !input) {
      setDeviceStatus("Select MIDI ports", "error");
      sendDisabled.value = true;
      return;
    }

    setDeviceStatus("Identifying device…", "busy");
    sendDisabled.value = true;

    const expected = deviceForTarget(pendingTarget.value);
    try {
      const identity = await detectDevice(output, input, { channel: channel.value });
      if (inquiryToken !== deviceInquiryToken) {
        return;
      }
      if (identity.deviceId !== expected.id) {
        setDeviceStatus(`This port is ${identity.shortLabel}, not ${expected.shortLabel}.`, "error");
        log(`Expected ${expected.shortLabel}, got ${identity.label}`, "warn");
        sendDisabled.value = true;
        return;
      }
      setDeviceStatus(formatDeviceStatus(identity, output), "ok");
      log(`Device identified: ${identity.label}`);
      sendDisabled.value = false;
      if (pendingPlugin.value) {
        const module = moduleFor(pendingPlugin.value, pendingTarget.value);
        await inquireSlotOccupancy(module);
        if (inquiryToken !== deviceInquiryToken) {
          return;
        }
        setDeviceStatus(formatDeviceStatus(identity, output), "ok");
      }
    } catch (error) {
      if (inquiryToken !== deviceInquiryToken) {
        return;
      }
      setDeviceStatus(`No ${expected.shortLabel} device found. Check USB connection and channel.`, "error");
      log(`Device inquiry failed: ${error.message}`, "warn");
      sendDisabled.value = true;
    }
  }

  function openSendModal(plugin, target = "nts-1_mkii") {
    pendingPlugin.value = plugin;
    pendingTarget.value = target;

    const device = deviceForTarget(target);
    midiHint.value = `If two ${device.shortLabel} ports appear, use the last one.`;

    clearLog();
    resetSlotStatuses();
    currentSlotModule.value = "";
    applySlotModule(moduleFor(plugin, target));
    sendDisabled.value = true;

    if (!webMidiSupported.value) {
      setDeviceStatus("Use Chrome or Edge for MIDI", "warn");
    } else {
      setDeviceStatus(`Looking for ${device.shortLabel}…`, "busy");
    }

    isOpen.value = true;
    document.body.classList.add("modal-open");

    if (webMidiSupported.value) {
      inquireDevice();
    }
  }

  function closeSendModal() {
    isOpen.value = false;
    document.body.classList.remove("modal-open");
    pendingPlugin.value = null;
    deviceInquiryToken += 1;
    slotInquiryToken += 1;
  }

  async function fetchUnit(plugin, target) {
    const cacheKey = `${plugin.id}:${target}`;
    if (unitCache.has(cacheKey)) {
      return unitCache.get(cacheKey);
    }
    const build = (plugin.builds || []).find((entry) => entry.target === target);
    if (!build) {
      throw new Error(`No build for ${plugin.id} / ${target}`);
    }
    const response = await fetch(build.file);
    if (!response.ok) {
      throw new Error(`Could not fetch ${build.file}`);
    }
    const bytes = new Uint8Array(await response.arrayBuffer());
    unitCache.set(cacheKey, bytes);
    return bytes;
  }

  async function sendPlugin() {
    const plugin = pendingPlugin.value;
    const target = pendingTarget.value;
    if (!plugin) {
      return;
    }

    if (!midiAccess.value) {
      const connected = await connectMidi();
      if (!connected) {
        setDeviceStatus("MIDI permission denied", "error");
        return;
      }
    }

    const output = selectedPort(selectedOutputId.value, midiAccess.value.outputs);
    const input = selectedPort(selectedInputId.value, midiAccess.value.inputs);
    if (!output || !input) {
      setDeviceStatus("Select MIDI ports", "error");
      return;
    }

    const module = moduleFor(plugin, target);
    applySlotModule(module);

    sendDisabled.value = true;
    setDeviceStatus("Sending…", "busy");

    try {
      const device = deviceForTarget(target);
      try {
        const identity = await detectDevice(output, input, { channel: channel.value });
        if (identity.deviceId !== device.id) {
          setDeviceStatus(`This port is ${identity.shortLabel}, not ${device.shortLabel}.`, "error");
          log(`Expected ${device.shortLabel}, got ${identity.label}`, "error");
          return;
        }
        log(`Device identified: ${identity.label}`);
        setDeviceStatus(formatDeviceStatus(identity, output), "ok");
      } catch (error) {
        setDeviceStatus(`No ${device.shortLabel} device found. Check USB connection and channel.`, "error");
        log(`Device inquiry failed: ${error.message}`, "error");
        return;
      }

      const unitBytes = await fetchUnit(plugin, target);
      log(`Loaded ${plugin.id} for ${target} (${unitBytes.length} bytes)`);

      try {
        const slotInfo = await readSlotStatus(output, input, {
          module,
          slot: slot.value,
          channel: channel.value,
          device,
        });
        if (slotInfo.empty) {
          log(`${module} slot ${slot.value} is empty`);
        } else {
          const loadedName = slotInfo.name || "occupied";
          log(`${module} slot ${slot.value} currently has ${loadedName} and will be overwritten`);
        }
      } catch (error) {
        log(`Slot inquiry skipped: ${error.message}`, "warn");
      }

      await installUnit(output, input, unitBytes, {
        module,
        slot: slot.value,
        channel: channel.value,
        device,
        onProgress: ({ phase, packetIndex, packetCount }) => {
          if (phase === "start") {
            log(`Sending ${packetCount} SysEx packet(s) to ${module} slot ${slot.value}`);
          }
          if (phase === "packet") {
            setDeviceStatus(`Sent packet ${packetIndex} / ${packetCount}`, "busy");
          }
        },
      });

      setDeviceStatus(`${plugin.name} → ${module} ${slot.value}`, "ok");
      log(LOAD_HINT[module] || "Load it on the device.", "ok");
      await inquireSlotOccupancy(module);
    } catch (error) {
      setDeviceStatus("Transfer failed", "error");
      log(error.message, "error");
    } finally {
      sendDisabled.value = false;
    }
  }

  function onMidiSettingChange() {
    inquireDevice();
  }

  return {
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
  };
}
