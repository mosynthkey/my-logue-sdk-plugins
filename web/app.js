import {
  installUnit,
  pickPreferredPort,
  detectDevice,
  readSlotStatus,
  readModuleSlots,
  MODULE_SLOTS,
  DEVICES,
  NTS1_MKII,
  NTS3_KAOSS,
  looksLikeDevicePort,
  listMidiPorts,
  portLabel,
} from "./nts1-midi.js";

const logEl = document.getElementById("log");
const pluginListEl = document.getElementById("plugin-list");
const outputSelect = document.getElementById("midi-output");
const inputSelect = document.getElementById("midi-input");
const slotSelect = document.getElementById("slot");
const sendButton = document.getElementById("send");
const channelInput = document.getElementById("channel");
const midiPanel = document.getElementById("midi-panel");
const midiUnsupported = document.getElementById("midi-unsupported");
const slotLabel = document.getElementById("slot-label");
const sendModal = document.getElementById("send-modal");
const sendModalTitle = document.getElementById("send-modal-title");
const sendModalKicker = document.getElementById("send-modal-kicker");
const deviceStatusEl = document.getElementById("device-status");
const midiHint = document.getElementById("midi-hint");

let midiAccess = null;
let pendingPlugin = null;
let pendingTarget = "nts-1_mkii";
let deviceInquiryToken = 0;
let slotInquiryToken = 0;
let currentSlotModule = "osc";
const unitCache = new Map();
const slotStatuses = new Map();

const LOAD_HINT = {
  osc: "Select OSC.",
  delfx: "Select DELAY.",
  revfx: "Select REVERB.",
  modfx: "Select MOD.",
  genericfx: "The unit appears at the end of the FX list.",
};

const SENDABLE_TARGETS = new Set(["nts-1_mkii", "nts-3_kaoss"]);

const TARGET_LABEL = {
  "nts-1_mkii": "mkII",
  "nts-3_kaoss": "NTS-3",
};

function targetName(target) {
  return TARGET_LABEL[target] || target;
}

function deviceForTarget(target) {
  return DEVICES[target] || NTS1_MKII;
}

function sendLabel(target) {
  return `Send to ${deviceForTarget(target).shortLabel}`;
}

function log(message, kind = "info") {
  const line = document.createElement("p");
  line.className = `log-line log-${kind}`;
  line.textContent = message;
  logEl.prepend(line);
}

function clearLog() {
  logEl.innerHTML = "";
}

function setDeviceStatus(text, kind = "idle") {
  deviceStatusEl.dataset.kind = kind;
  deviceStatusEl.textContent = text;
}

function formatDeviceStatus(identity, output) {
  const deviceName = identity?.label || deviceForTarget(pendingTarget).shortLabel;
  return `${deviceName} on ${portLabel(output)}`;
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

function populateSlotOptions(module = "osc") {
  const count = MODULE_SLOTS[module] || 16;
  const previous = slotSelect.value;
  slotSelect.innerHTML = "";
  for (let slotIndex = 0; slotIndex < count; slotIndex++) {
    const option = document.createElement("option");
    option.value = String(slotIndex);
    option.textContent = slotOptionLabel(slotIndex, slotStatuses.get(slotIndex));
    slotSelect.append(option);
  }
  if ([...slotSelect.options].some((option) => option.value === previous)) {
    slotSelect.value = previous;
  } else {
    slotSelect.value = "1";
  }
}

function resetSlotStatuses() {
  slotStatuses.clear();
}

function moduleFor(plugin, target) {
  const build = (plugin.builds || []).find((entry) => entry.target === target);
  return (build && build.module) || plugin.module || "osc";
}

function applySlotModule(module) {
  const previous = Number(slotSelect.value);
  if (module !== currentSlotModule) {
    resetSlotStatuses();
    currentSlotModule = module;
  }
  populateSlotOptions(module);
  const maxSlot = (MODULE_SLOTS[module] || 16) - 1;
  const nextSlot = Number.isFinite(previous) ? Math.min(Math.max(previous, 0), maxSlot) : 1;
  slotSelect.value = String(nextSlot);
  if (slotLabel) {
    slotLabel.textContent = `${module} slot`;
  }
}

async function inquireSlotOccupancy(module) {
  const inquiryToken = ++slotInquiryToken;
  if (!midiAccess || !hasMidiPorts()) {
    resetSlotStatuses();
    populateSlotOptions(module);
    return;
  }

  const output = selectedPort(outputSelect, midiAccess.outputs);
  const input = selectedPort(inputSelect, midiAccess.inputs);
  if (!output || !input) {
    return;
  }

  const channel = Number(channelInput.value) || 1;
  try {
    const slots = await readModuleSlots(output, input, {
      module,
      channel,
      device: deviceForTarget(pendingTarget),
    });
    if (inquiryToken !== slotInquiryToken) {
      return;
    }
    slotStatuses.clear();
    for (const status of slots) {
      slotStatuses.set(status.slot, status);
    }
    populateSlotOptions(module);
  } catch (error) {
    if (inquiryToken !== slotInquiryToken) {
      return;
    }
    log(`Slot occupancy inquiry failed: ${error.message}`, "warn");
  }
}

function fillPortSelect(select, ports, preferred) {
  select.innerHTML = "";
  const listed = listMidiPorts(ports);
  if (listed.length === 0) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "No ports";
    select.append(option);
    return;
  }
  for (const port of listed) {
    const option = document.createElement("option");
    option.value = port.id;
    option.textContent = portLabel(port) + portSuffix(port.name);
    select.append(option);
  }
  if (preferred) {
    select.value = preferred.id;
  }
}

function selectedPort(select, ports) {
  return ports.get(select.value) || null;
}

function hasMidiPorts() {
  return midiAccess && midiAccess.outputs.size > 0 && midiAccess.inputs.size > 0;
}

async function refreshPorts() {
  if (!midiAccess) {
    return;
  }
  const preferredDevice = deviceForTarget(pendingTarget);
  fillPortSelect(outputSelect, midiAccess.outputs, pickPreferredPort(midiAccess.outputs, preferredDevice));
  fillPortSelect(inputSelect, midiAccess.inputs, pickPreferredPort(midiAccess.inputs, preferredDevice));
}

async function connectMidi() {
  if (!navigator.requestMIDIAccess) {
    return false;
  }

  if (midiAccess) {
    return true;
  }

  try {
    midiAccess = await navigator.requestMIDIAccess({ sysex: true });
  } catch (error) {
    log(String(error), "error");
    return false;
  }

  midiAccess.onstatechange = () => {
    refreshPorts().then(() => inquireDevice());
  };
  await refreshPorts();
  log("SysEx enabled.");
  return true;
}

async function inquireDevice() {
  const inquiryToken = ++deviceInquiryToken;

  if (!navigator.requestMIDIAccess) {
    setDeviceStatus("Use Chrome or Edge for MIDI", "warn");
    sendButton.disabled = true;
    return;
  }

  if (!midiAccess) {
    setDeviceStatus(`Looking for ${deviceForTarget(pendingTarget).shortLabel}…`, "busy");
    sendButton.disabled = true;
    const connected = await connectMidi();
    if (!connected || inquiryToken !== deviceInquiryToken) {
      return;
    }
  }

  if (!hasMidiPorts()) {
    setDeviceStatus(`No MIDI device found. Connect ${deviceForTarget(pendingTarget).shortLabel} over USB-C.`, "error");
    sendButton.disabled = true;
    return;
  }

  const output = selectedPort(outputSelect, midiAccess.outputs);
  const input = selectedPort(inputSelect, midiAccess.inputs);
  if (!output || !input) {
    setDeviceStatus("Select MIDI ports", "error");
    sendButton.disabled = true;
    return;
  }

  const channel = Number(channelInput.value) || 1;
  setDeviceStatus("Identifying device…", "busy");
  sendButton.disabled = true;

  const expected = deviceForTarget(pendingTarget);
  try {
    const identity = await detectDevice(output, input, { channel });
    if (inquiryToken !== deviceInquiryToken) {
      return;
    }
    if (identity.deviceId !== expected.id) {
      setDeviceStatus(`This port is ${identity.shortLabel}, not ${expected.shortLabel}.`, "error");
      log(`Expected ${expected.shortLabel}, got ${identity.label}`, "warn");
      sendButton.disabled = true;
      return;
    }
    setDeviceStatus(formatDeviceStatus(identity, output), "ok");
    log(`Device identified: ${identity.label}`);
    sendButton.disabled = false;
    if (pendingPlugin) {
      const module = moduleFor(pendingPlugin, pendingTarget);
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
    sendButton.disabled = true;
  }
}

function openSendModal(plugin, target = "nts-1_mkii") {
  pendingPlugin = plugin;
  pendingTarget = target;
  const device = deviceForTarget(target);
  sendModalTitle.textContent = plugin.name;
  if (sendModalKicker) {
    sendModalKicker.textContent = sendLabel(target);
  }
  if (midiHint) {
    midiHint.textContent = `If two ${device.shortLabel} ports appear, use the last one.`;
  }
  clearLog();
  resetSlotStatuses();
  currentSlotModule = "";
  applySlotModule(moduleFor(plugin, target));

  const webMidiSupported = Boolean(navigator.requestMIDIAccess);
  midiUnsupported.hidden = webMidiSupported;
  midiPanel.hidden = !webMidiSupported;
  sendButton.disabled = true;

  if (!webMidiSupported) {
    setDeviceStatus("Use Chrome or Edge for MIDI", "warn");
  } else {
    setDeviceStatus(`Looking for ${device.shortLabel}…`, "busy");
  }

  sendModal.hidden = false;
  document.body.classList.add("modal-open");

  if (webMidiSupported) {
    inquireDevice();
  }
}

function closeSendModal() {
  sendModal.hidden = true;
  document.body.classList.remove("modal-open");
  pendingPlugin = null;
  deviceInquiryToken += 1;
  slotInquiryToken += 1;
}

async function loadCatalog() {
  const response = await fetch("./catalog.json", { cache: "no-store" });
  if (!response.ok) {
    throw new Error("catalog.json is missing. Build the site first.");
  }
  return response.json();
}

function renderPlugins(catalog) {
  pluginListEl.innerHTML = "";
  for (const plugin of catalog.plugins) {
    const builds = plugin.builds || [];
    const targetLabel = builds.map((build) => targetName(build.target)).join(" / ") || "unbuilt";
    const actions = builds
      .map((build) => {
        const send = SENDABLE_TARGETS.has(build.target)
          ? `<button class="button button-primary" data-send="${plugin.id}" data-target="${build.target}">${sendLabel(build.target)}</button>`
          : "";
        const wasm = build.wasm
          ? `<a class="button button-ghost" href="${build.wasm}">Preview</a>`
          : "";
        return `<a class="button button-secondary" href="${build.file}" download>Download ${targetName(build.target)}</a>${wasm}${send}`;
      })
      .join("");
    const card = document.createElement("article");
    card.className = "plugin-card";
    card.innerHTML = `
      <header class="plugin-card__head">
        <p class="kicker">${plugin.type.toUpperCase()} · ${targetLabel}</p>
        <h2>${plugin.name}</h2>
      </header>
      <p class="plugin-card__desc">${plugin.description}</p>
      <ul class="params">
        ${plugin.params
          .map((param) => `<li><span>${param.name}</span><em>${param.role}</em> ${param.detail}</li>`)
          .join("")}
      </ul>
      <div class="plugin-card__actions">${actions}</div>
    `;
    pluginListEl.append(card);
  }
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

async function sendPlugin(plugin, target = "nts-1_mkii") {
  if (!midiAccess) {
    const connected = await connectMidi();
    if (!connected) {
      setDeviceStatus("MIDI permission denied", "error");
      return;
    }
  }

  const output = selectedPort(outputSelect, midiAccess.outputs);
  const input = selectedPort(inputSelect, midiAccess.inputs);
  if (!output || !input) {
    setDeviceStatus("Select MIDI ports", "error");
    return;
  }

  const module = moduleFor(plugin, target);
  applySlotModule(module);

  const channel = Number(channelInput.value) || 1;
  const slot = Number(slotSelect.value);
  sendButton.disabled = true;
  setDeviceStatus("Sending…", "busy");

  try {
    const device = deviceForTarget(target);
    try {
      const identity = await detectDevice(output, input, { channel });
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
        slot,
        channel,
        device,
      });
      if (slotInfo.empty) {
        log(`${module} slot ${slot} is empty`);
      } else {
        const loadedName = slotInfo.name || "occupied";
        log(`${module} slot ${slot} currently has ${loadedName} and will be overwritten`);
      }
    } catch (error) {
      log(`Slot inquiry skipped: ${error.message}`, "warn");
    }

    await installUnit(output, input, unitBytes, {
      module,
      slot,
      channel,
      device,
      onProgress: ({ phase, packetIndex, packetCount }) => {
        if (phase === "start") {
          log(`Sending ${packetCount} SysEx packet(s) to ${module} slot ${slot}`);
        }
        if (phase === "packet") {
          setDeviceStatus(`Sent packet ${packetIndex} / ${packetCount}`, "busy");
        }
      },
    });

    setDeviceStatus(`${plugin.name} → ${module} ${slot}`, "ok");
    log(LOAD_HINT[module] || "Load it on the device.", "ok");
    await inquireSlotOccupancy(module);
  } catch (error) {
    setDeviceStatus("Transfer failed", "error");
    log(error.message, "error");
  } finally {
    sendButton.disabled = false;
  }
}

async function main() {
  applySlotModule("osc");

  let catalog;
  try {
    catalog = await loadCatalog();
  } catch (error) {
    pluginListEl.innerHTML = `<p class="empty">${error.message}</p>`;
    return;
  }

  renderPlugins(catalog);

  pluginListEl.addEventListener("click", (event) => {
    const button = event.target.closest("[data-send]");
    if (!button) {
      return;
    }
    const plugin = catalog.plugins.find((entry) => entry.id === button.dataset.send);
    if (plugin) {
      const target = button.dataset.target || "nts-1_mkii";
      openSendModal(plugin, target);
    }
  });

  sendButton.addEventListener("click", async () => {
    if (pendingPlugin) {
      await sendPlugin(pendingPlugin, pendingTarget);
    }
  });

  outputSelect.addEventListener("change", () => {
    inquireDevice();
  });
  inputSelect.addEventListener("change", () => {
    inquireDevice();
  });
  channelInput.addEventListener("change", () => {
    inquireDevice();
  });

  sendModal.addEventListener("click", (event) => {
    if (event.target.closest("[data-close-modal]")) {
      closeSendModal();
    }
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && !sendModal.hidden) {
      closeSendModal();
    }
  });
}

main();
