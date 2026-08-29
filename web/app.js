import {
  installUnit,
  pickPreferredPort,
  detectDevice,
  readSlotStatus,
  MODULE_SLOTS,
  looksLikeNts1Name,
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
const deviceStatusEl = document.getElementById("device-status");

let midiAccess = null;
let pendingPlugin = null;
let pendingTarget = "nts-1_mkii";
let deviceInquiryToken = 0;
const unitCache = new Map();

const LOAD_HINT = {
  osc: "Select OSC.",
  delfx: "Select DELAY.",
  revfx: "Select REVERB.",
  modfx: "Select MOD.",
};

const TARGET_LABEL = {
  "nts-1_mkii": "mkII",
  "nts-3_kaoss": "NTS-3",
};

function targetName(target) {
  return TARGET_LABEL[target] || target;
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
  const deviceName = identity?.label || "NTS-1 mkII";
  return `${deviceName} on ${portLabel(output)}`;
}

function populateSlotOptions(module = "osc") {
  const count = MODULE_SLOTS[module] || 16;
  slotSelect.innerHTML = "";
  for (let slotIndex = 0; slotIndex < count; slotIndex++) {
    const option = document.createElement("option");
    option.value = String(slotIndex);
    option.textContent = `Slot ${slotIndex}`;
    slotSelect.append(option);
  }
  slotSelect.value = "1";
}

function moduleFor(plugin, target) {
  const build = (plugin.builds || []).find((entry) => entry.target === target);
  return (build && build.module) || plugin.module || "osc";
}

function applySlotModule(module) {
  const previous = Number(slotSelect.value);
  populateSlotOptions(module);
  const maxSlot = (MODULE_SLOTS[module] || 16) - 1;
  const nextSlot = Number.isFinite(previous) ? Math.min(Math.max(previous, 0), maxSlot) : 1;
  slotSelect.value = String(nextSlot);
  if (slotLabel) {
    slotLabel.textContent = `${module} slot`;
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
    option.textContent = portLabel(port) + (looksLikeNts1Name(port.name) ? "  · NTS-1" : "");
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
  fillPortSelect(outputSelect, midiAccess.outputs, pickPreferredPort(midiAccess.outputs));
  fillPortSelect(inputSelect, midiAccess.inputs, pickPreferredPort(midiAccess.inputs));
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
    setDeviceStatus("Looking for NTS-1 mkII…", "busy");
    sendButton.disabled = true;
    const connected = await connectMidi();
    if (!connected || inquiryToken !== deviceInquiryToken) {
      return;
    }
  }

  if (!hasMidiPorts()) {
    setDeviceStatus("No MIDI device found. Connect NTS-1 mkII over USB-C.", "error");
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

  try {
    const identity = await detectDevice(output, input, { channel });
    if (inquiryToken !== deviceInquiryToken) {
      return;
    }
    setDeviceStatus(formatDeviceStatus(identity, output), "ok");
    log(`Device identified: ${identity.label}`);
    sendButton.disabled = false;
  } catch (error) {
    if (inquiryToken !== deviceInquiryToken) {
      return;
    }
    setDeviceStatus("No NTS-1 mkII device found. Check USB connection and channel.", "error");
    log(`Device inquiry failed: ${error.message}`, "warn");
    sendButton.disabled = true;
  }
}

function openSendModal(plugin, target = "nts-1_mkii") {
  pendingPlugin = plugin;
  pendingTarget = target;
  sendModalTitle.textContent = plugin.name;
  clearLog();
  applySlotModule(moduleFor(plugin, target));

  const webMidiSupported = Boolean(navigator.requestMIDIAccess);
  midiUnsupported.hidden = webMidiSupported;
  midiPanel.hidden = !webMidiSupported;
  sendButton.disabled = true;

  if (!webMidiSupported) {
    setDeviceStatus("Use Chrome or Edge for MIDI", "warn");
  } else {
    setDeviceStatus("Looking for NTS-1 mkII…", "busy");
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
}

async function loadCatalog() {
  const response = await fetch("./catalog.json", { cache: "no-store" });
  if (!response.ok) {
    throw new Error("catalog.json is missing. Build the site first.");
  }
  return response.json();
}

function hashSeed(text) {
  let seed = 0;
  for (let charIndex = 0; charIndex < text.length; charIndex++) {
    seed = (seed * 31 + text.charCodeAt(charIndex)) | 0;
  }
  return Math.abs(seed);
}

function pseudoRandom(seed) {
  const value = Math.sin(seed) * 10000;
  return value - Math.floor(value);
}

function buildArcViz(pluginId) {
  const seed = hashSeed(pluginId);
  const gradId = `arc-grad-${pluginId}`;
  const arcs = [];
  const arcCount = 6;
  for (let arcIndex = 0; arcIndex < arcCount; arcIndex++) {
    const radius = 28 + arcIndex * 14;
    const dash = 40 + pseudoRandom(seed + arcIndex) * 80;
    const offset = pseudoRandom(seed + arcIndex * 3) * 200;
    arcs.push(
      `<circle cx="50%" cy="55%" r="${radius}" fill="none" stroke="url(#${gradId})" stroke-width="0.5" stroke-dasharray="${dash} ${220 - dash}" stroke-dashoffset="${offset}" opacity="${0.25 + arcIndex * 0.12}"/>`
    );
  }
  return `<svg class="plugin-card__viz-canvas" viewBox="0 0 400 220" preserveAspectRatio="xMidYMid slice" aria-hidden="true">
    <defs>
      <linearGradient id="${gradId}" x1="0%" y1="0%" x2="100%" y2="100%">
        <stop offset="0%" stop-color="#4c1d95"/>
        <stop offset="50%" stop-color="#8b5cf6"/>
        <stop offset="100%" stop-color="#c4b5fd"/>
      </linearGradient>
    </defs>
    ${arcs.join("")}
  </svg>`;
}

function buildParamMeter(pluginId, paramIndex) {
  const seed = hashSeed(`${pluginId}:${paramIndex}`);
  const barCount = 12;
  const bars = [];
  for (let barIndex = 0; barIndex < barCount; barIndex++) {
    const height = 20 + pseudoRandom(seed + barIndex * 7) * 80;
    bars.push(`<span class="param-meter__bar" style="height:${height.toFixed(1)}%"></span>`);
  }
  return `<span class="param-meter" aria-hidden="true">${bars.join("")}</span>`;
}

function renderPlugins(catalog) {
  pluginListEl.innerHTML = "";
  for (const plugin of catalog.plugins) {
    const builds = plugin.builds || [];
    const targetLabel = builds.map((build) => targetName(build.target)).join(" / ") || "unbuilt";
    const actions = builds
      .map((build) => {
        const send =
          build.target === "nts-1_mkii"
            ? `<button class="button button-primary" data-send="${plugin.id}" data-target="${build.target}">Send</button>`
            : "";
        const wasm = build.wasm
          ? `<a class="button button-ghost" href="${build.wasm}">Preview</a>`
          : "";
        return `<a class="button button-secondary" href="${build.file}" download>${targetName(build.target)}</a>${wasm}${send}`;
      })
      .join("");
    const card = document.createElement("article");
    card.className = "plugin-card";
    card.innerHTML = `
      <div class="plugin-card__viz">
        ${buildArcViz(plugin.id)}
        <p class="plugin-card__viz-label">${plugin.type} · ${targetLabel}</p>
        <h2>${plugin.name}</h2>
        <p class="plugin-card__desc">${plugin.description}</p>
      </div>
      <div class="plugin-card__controls">
        <ul class="params">
          ${plugin.params
            .map(
              (param, paramIndex) =>
                `<li>${buildParamMeter(plugin.id, paramIndex)}<span>${param.name}</span><em>${param.role}</em> ${param.detail}</li>`
            )
            .join("")}
        </ul>
        <div class="plugin-card__actions">${actions}</div>
      </div>
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
    try {
      const identity = await detectDevice(output, input, { channel });
      log(`Device identified: ${identity.label}`);
      setDeviceStatus(formatDeviceStatus(identity, output), "ok");
    } catch (error) {
      setDeviceStatus("No NTS-1 mkII device found. Check USB connection and channel.", "error");
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
      });
      log(slotInfo.empty ? `${module} slot ${slot} is empty` : `${module} slot ${slot} is occupied and will be overwritten`);
    } catch (error) {
      log(`Slot inquiry skipped: ${error.message}`, "warn");
    }

    await installUnit(output, input, unitBytes, {
      module,
      slot,
      channel,
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
