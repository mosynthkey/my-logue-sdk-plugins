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
import { LOAD_HINT, SENDABLE_TARGETS } from "../constants.js";
import { moduleFor, sendableBuilds } from "../utils/plugin.js";

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

function emptyConnectedTargets() {
  return {
    "nts-1_mkii": false,
    "nts-3_kaoss": false,
  };
}

function buildSlotOptions(module, statuses) {
  const count = MODULE_SLOTS[module] || 16;
  const options = [];
  for (let slotIndex = 0; slotIndex < count; slotIndex += 1) {
    const status = statuses?.get(slotIndex) ?? null;
    options.push({
      value: slotIndex,
      label: slotOptionLabel(slotIndex, status),
      empty: status ? Boolean(status.empty) : true,
      name: status && !status.empty ? (status.name || "occupied") : "",
    });
  }
  return options;
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

  const midiAccess = ref(null);
  let deviceInquiryToken = 0;
  let slotInquiryToken = 0;
  let inlineSyncToken = 0;
  const currentSlotModule = ref("osc");
  const slotStatuses = ref(new Map());
  const slotStatusVersion = ref(0);
  const unitCache = new Map();
  const connectedTargets = ref(emptyConnectedTargets());
  const inlinePlugin = ref(null);
  const inlineSlotsByTarget = ref({});
  const inlineSlotsLoading = ref({});
  const inlineStatusText = ref("");
  const inlineStatusKind = ref("idle");
  const sending = ref(false);

  const nts3Connected = computed(() => connectedTargets.value["nts-3_kaoss"]);

  const slotOptions = computed(() => {
    slotStatusVersion.value;
    return buildSlotOptions(currentSlotModule.value, slotStatuses.value);
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

  function setInlineStatus(text, kind = "idle") {
    inlineStatusText.value = text;
    inlineStatusKind.value = kind;
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

  function portsForTarget(target) {
    if (!midiAccess.value) {
      return { output: null, input: null };
    }
    const device = deviceForTarget(target);
    return {
      output: pickPreferredPort(midiAccess.value.outputs, device),
      input: pickPreferredPort(midiAccess.value.inputs, device),
    };
  }

  const selectedOutputLabel = computed(() => {
    if (!midiAccess.value || !selectedOutputId.value) {
      return "";
    }
    const port = selectedPort(selectedOutputId.value, midiAccess.value.outputs);
    return port ? portLabel(port) + portSuffix(port.name) : "";
  });

  const selectedInputLabel = computed(() => {
    if (!midiAccess.value || !selectedInputId.value) {
      return "";
    }
    const port = selectedPort(selectedInputId.value, midiAccess.value.inputs);
    return port ? portLabel(port) + portSuffix(port.name) : "";
  });

  function updateConnectedTargets() {
    if (!midiAccess.value) {
      connectedTargets.value = emptyConnectedTargets();
      return;
    }
    const ports = [
      ...listMidiPorts(midiAccess.value.outputs),
      ...listMidiPorts(midiAccess.value.inputs),
    ];
    const next = emptyConnectedTargets();
    for (const target of SENDABLE_TARGETS) {
      const device = deviceForTarget(target);
      next[target] = ports.some((port) => looksLikeDevicePort(port.name, device));
    }
    connectedTargets.value = next;
  }

  function refreshPortLists() {
    if (!midiAccess.value) {
      outputPorts.value = [];
      inputPorts.value = [];
      updateConnectedTargets();
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
    } else {
      selectedOutputId.value = "";
    }

    if (preferredInput) {
      selectedInputId.value = preferredInput.id;
    } else if (inputPorts.value.length > 0) {
      selectedInputId.value = inputPorts.value[0].id;
    } else {
      selectedInputId.value = "";
    }

    updateConnectedTargets();
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

  function clearInlineSlots() {
    inlineSlotsByTarget.value = {};
    inlineSlotsLoading.value = {};
  }

  function seedInlineSlots(plugin) {
    const nextSlots = {};
    const nextLoading = {};
    for (const build of sendableBuilds(plugin)) {
      if (!connectedTargets.value[build.target]) {
        continue;
      }
      const module = moduleFor(plugin, build.target);
      nextSlots[build.target] = buildSlotOptions(module, null);
      nextLoading[build.target] = true;
    }
    inlineSlotsByTarget.value = nextSlots;
    inlineSlotsLoading.value = nextLoading;
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
      if (isOpen.value) {
        inquireDevice();
      } else if (inlinePlugin.value) {
        syncInlineSlots(inlinePlugin.value);
      }
    };
    refreshPortLists();
    log("SysEx enabled.");
    return true;
  }

  async function startPresenceWatch() {
    if (!webMidiSupported.value) {
      connectedTargets.value = emptyConnectedTargets();
      return false;
    }
    const connected = await connectMidi();
    if (!connected) {
      connectedTargets.value = emptyConnectedTargets();
      return false;
    }
    updateConnectedTargets();
    return true;
  }

  async function inquireSlotOccupancy(module, target = pendingTarget.value) {
    const inquiryToken = ++slotInquiryToken;
    const { output, input } = portsForTarget(target);
    if (!output || !input) {
      if (target === pendingTarget.value && module === currentSlotModule.value) {
        resetSlotStatuses();
      }
      return null;
    }

    try {
      const slots = await readModuleSlots(output, input, {
        module,
        channel: channel.value,
        device: deviceForTarget(target),
      });
      const nextStatuses = new Map();
      for (const status of slots) {
        nextStatuses.set(status.slot, status);
      }
      if (
        inquiryToken === slotInquiryToken
        && target === pendingTarget.value
        && module === currentSlotModule.value
      ) {
        slotStatuses.value = nextStatuses;
        slotStatusVersion.value += 1;
      }
      return nextStatuses;
    } catch (error) {
      log(`Slot occupancy inquiry failed: ${error.message}`, "warn");
      return null;
    }
  }

  async function syncInlineSlots(plugin) {
    const syncToken = ++inlineSyncToken;
    inlinePlugin.value = plugin || null;

    if (!plugin) {
      clearInlineSlots();
      return;
    }

    if (!webMidiSupported.value || !midiAccess.value) {
      clearInlineSlots();
      return;
    }

    seedInlineSlots(plugin);

    const builds = sendableBuilds(plugin).filter((build) => connectedTargets.value[build.target]);
    if (builds.length === 0) {
      clearInlineSlots();
      return;
    }

    await Promise.all(builds.map(async (build) => {
      const target = build.target;
      const module = moduleFor(plugin, target);
      const { output, input } = portsForTarget(target);
      if (!output || !input) {
        if (syncToken !== inlineSyncToken) {
          return;
        }
        inlineSlotsLoading.value = { ...inlineSlotsLoading.value, [target]: false };
        return;
      }

      try {
        const identity = await detectDevice(output, input);
        if (syncToken !== inlineSyncToken) {
          return;
        }
        if (identity.deviceId !== deviceForTarget(target).id) {
          const nextSlots = { ...inlineSlotsByTarget.value };
          delete nextSlots[target];
          inlineSlotsByTarget.value = nextSlots;
          inlineSlotsLoading.value = { ...inlineSlotsLoading.value, [target]: false };
          return;
        }
        if (identity.midiChannel != null) {
          channel.value = identity.midiChannel;
        }

        const statuses = await inquireSlotOccupancy(module, target);
        if (syncToken !== inlineSyncToken) {
          return;
        }
        inlineSlotsByTarget.value = {
          ...inlineSlotsByTarget.value,
          [target]: buildSlotOptions(module, statuses),
        };
      } catch {
        if (syncToken !== inlineSyncToken) {
          return;
        }
        // Keep numbered slots so the user can still send without occupancy labels.
      } finally {
        if (syncToken === inlineSyncToken) {
          inlineSlotsLoading.value = { ...inlineSlotsLoading.value, [target]: false };
        }
      }
    }));
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

    const { output, input } = portsForTarget(pendingTarget.value);
    if (output) {
      selectedOutputId.value = output.id;
    }
    if (input) {
      selectedInputId.value = input.id;
    }
    if (!output || !input) {
      setDeviceStatus(
        `Connect ${deviceForTarget(pendingTarget.value).shortLabel} over USB.`,
        "error",
      );
      sendDisabled.value = true;
      return;
    }

    setDeviceStatus("Identifying device…", "busy");
    sendDisabled.value = true;

    const expected = deviceForTarget(pendingTarget.value);
    try {
      const identity = await detectDevice(output, input);
      if (inquiryToken !== deviceInquiryToken) {
        return;
      }
      if (identity.deviceId !== expected.id) {
        setDeviceStatus(`This port is ${identity.shortLabel}, not ${expected.shortLabel}.`, "error");
        log(`Expected ${expected.shortLabel}, got ${identity.label}`, "warn");
        sendDisabled.value = true;
        return;
      }
      if (identity.midiChannel != null) {
        channel.value = identity.midiChannel;
      }
      setDeviceStatus(formatDeviceStatus(identity, output), "ok");
      log(`Device identified: ${identity.label}${identity.midiChannel != null ? ` · ch ${identity.midiChannel}` : ""}`);
      sendDisabled.value = false;
      if (pendingPlugin.value) {
        const module = moduleFor(pendingPlugin.value, pendingTarget.value);
        await inquireSlotOccupancy(module, pendingTarget.value);
        if (inquiryToken !== deviceInquiryToken) {
          return;
        }
        setDeviceStatus(formatDeviceStatus(identity, output), "ok");
      }
    } catch (error) {
      if (inquiryToken !== deviceInquiryToken) {
        return;
      }
      setDeviceStatus(`No ${expected.shortLabel} device found. Check USB connection.`, "error");
      log(`Device inquiry failed: ${error.message}`, "warn");
      sendDisabled.value = true;
    }
  }

  function openSendModal(plugin, target = "nts-1_mkii") {
    pendingPlugin.value = plugin;
    pendingTarget.value = target;

    const device = deviceForTarget(target);

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

  async function transferUnit(plugin, target, slotIndex, { useModalStatus }) {
    const setStatus = (text, kind) => {
      if (useModalStatus) {
        setDeviceStatus(text, kind);
      } else {
        setInlineStatus(text, kind);
      }
    };

    if (!midiAccess.value) {
      const connected = await connectMidi();
      if (!connected) {
        setStatus("MIDI permission denied", "error");
        return false;
      }
    }

    const { output, input } = portsForTarget(target);
    if (output) {
      selectedOutputId.value = output.id;
    }
    if (input) {
      selectedInputId.value = input.id;
    }
    if (!output || !input) {
      setStatus(`Connect ${deviceForTarget(target).shortLabel} over USB.`, "error");
      return false;
    }

    const module = moduleFor(plugin, target);
    applySlotModule(module);
    slot.value = slotIndex;

    sending.value = true;
    sendDisabled.value = true;
    setStatus("Sending…", "busy");

    try {
      const device = deviceForTarget(target);
      try {
        const identity = await detectDevice(output, input);
        if (identity.deviceId !== device.id) {
          setStatus(`This port is ${identity.shortLabel}, not ${device.shortLabel}.`, "error");
          log(`Expected ${device.shortLabel}, got ${identity.label}`, "error");
          return false;
        }
        if (identity.midiChannel != null) {
          channel.value = identity.midiChannel;
        }
        log(`Device identified: ${identity.label}${identity.midiChannel != null ? ` · ch ${identity.midiChannel}` : ""}`);
        if (useModalStatus) {
          setDeviceStatus(formatDeviceStatus(identity, output), "ok");
        }
      } catch (error) {
        setStatus(`No ${device.shortLabel} device found. Check USB connection.`, "error");
        log(`Device inquiry failed: ${error.message}`, "error");
        return false;
      }

      const unitBytes = await fetchUnit(plugin, target);
      log(`Loaded ${plugin.id} for ${target} (${unitBytes.length} bytes)`);

      try {
        const slotInfo = await readSlotStatus(output, input, {
          module,
          slot: slotIndex,
          channel: channel.value,
          device,
        });
        if (slotInfo.empty) {
          log(`${module} slot ${slotIndex} is empty`);
        } else {
          const loadedName = slotInfo.name || "occupied";
          log(`${module} slot ${slotIndex} currently has ${loadedName} and will be overwritten`);
        }
      } catch (error) {
        log(`Slot inquiry skipped: ${error.message}`, "warn");
      }

      await installUnit(output, input, unitBytes, {
        module,
        slot: slotIndex,
        channel: channel.value,
        device,
        onProgress: ({ phase, packetIndex, packetCount }) => {
          if (phase === "start") {
            log(`Sending ${packetCount} SysEx packet(s) to ${module} slot ${slotIndex}`);
          }
          if (phase === "packet") {
            setStatus(`Sent packet ${packetIndex} / ${packetCount}`, "busy");
          }
        },
      });

      setStatus(`${plugin.name} → ${module} ${slotIndex}`, "ok");
      log(LOAD_HINT[module] || "Load it on the device.", "ok");
      await inquireSlotOccupancy(module, target);
      if (!useModalStatus && inlinePlugin.value) {
        await syncInlineSlots(inlinePlugin.value);
      }
      return true;
    } catch (error) {
      setStatus("Transfer failed", "error");
      log(error.message, "error");
      return false;
    } finally {
      sending.value = false;
      sendDisabled.value = false;
    }
  }

  async function sendPlugin() {
    const plugin = pendingPlugin.value;
    const target = pendingTarget.value;
    if (!plugin) {
      return;
    }
    await transferUnit(plugin, target, slot.value, { useModalStatus: true });
  }

  async function sendToSlot(plugin, target, slotIndex) {
    if (!plugin || sending.value) {
      return;
    }
    pendingPlugin.value = plugin;
    pendingTarget.value = target;
    clearLog();
    await transferUnit(plugin, target, slotIndex, { useModalStatus: false });
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
    onMidiSettingChange,
    nts3Connected,
    connectedTargets,
    inlineSlotsByTarget,
    inlineSlotsLoading,
    inlineStatusText,
    inlineStatusKind,
    sending,
    syncInlineSlots,
    startPresenceWatch,
  };
}
