import {
  installUnit,
  pickPreferredPort,
  requestIdentity,
  readSlotStatus,
  MODULE_SLOTS,
  looksLikeNts1Name,
} from "./nts1-midi.js";

const logEl = document.getElementById("log");
const statusEl = document.getElementById("midi-status");
const pluginListEl = document.getElementById("plugin-list");
const outputSelect = document.getElementById("midi-output");
const inputSelect = document.getElementById("midi-input");
const slotSelect = document.getElementById("slot");
const sendButton = document.getElementById("send");
const connectButton = document.getElementById("connect");
const channelInput = document.getElementById("channel");
const midiPanel = document.getElementById("midi-panel");
const midiUnsupported = document.getElementById("midi-unsupported");
const slotLabel = document.getElementById("slot-label");

let midiAccess = null;
let lastMkiiPluginId = null;
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

function setStatus(text, kind = "idle") {
  statusEl.dataset.kind = kind;
  statusEl.textContent = text;
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

function mkiiBuild(plugin) {
  return (plugin.builds || []).find((entry) => entry.target === "nts-1_mkii");
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
  const listed = Array.from(ports);
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
    option.textContent = port.name + (looksLikeNts1Name(port.name) ? "  · NTS-1" : "");
    select.append(option);
  }
  if (preferred) {
    select.value = preferred.id;
  }
}

function selectedPort(select, ports) {
  return ports.get(select.value) || null;
}

async function refreshPorts() {
  if (!midiAccess) {
    return;
  }
  fillPortSelect(outputSelect, midiAccess.outputs, pickPreferredPort(midiAccess.outputs.values()));
  fillPortSelect(inputSelect, midiAccess.inputs, pickPreferredPort(midiAccess.inputs.values()));
}

async function connectMidi() {
  if (!navigator.requestMIDIAccess) {
    setStatus("Use Chrome or Edge", "error");
    midiUnsupported.hidden = false;
    midiPanel.hidden = true;
    return;
  }

  try {
    midiAccess = await navigator.requestMIDIAccess({ sysex: true });
  } catch (error) {
    setStatus("MIDI permission denied", "error");
    log(String(error), "error");
    return;
  }

  midiAccess.onstatechange = () => {
    refreshPorts();
  };
  await refreshPorts();
  setStatus("MIDI ready", "ok");
  log("SysEx enabled.");
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
        const send =
          build.target === "nts-1_mkii"
            ? `<button class="button button-primary" data-send="${plugin.id}" data-target="${build.target}">Send</button>`
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
    await connectMidi();
    if (!midiAccess) {
      return;
    }
  }

  const output = selectedPort(outputSelect, midiAccess.outputs);
  const input = selectedPort(inputSelect, midiAccess.inputs);
  if (!output || !input) {
    setStatus("Select MIDI ports", "error");
    return;
  }

  const module = moduleFor(plugin, target);
  applySlotModule(module);

  const channel = Number(channelInput.value) || 1;
  const slot = Number(slotSelect.value);
  sendButton.disabled = true;
  setStatus("Sending…", "busy");

  try {
    try {
      await requestIdentity(output, input, { channel });
      log(`Identity OK on ${output.name}`);
    } catch (error) {
      log(`Identity request failed (${error.message}). Sending anyway.`, "warn");
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
          setStatus(`Sent packet ${packetIndex} / ${packetCount}`, "busy");
        }
      },
    });

    setStatus(`${plugin.name} → ${module} ${slot}`, "ok");
    log(LOAD_HINT[module] || "Load it on the device.", "ok");
  } catch (error) {
    setStatus("Transfer failed", "error");
    log(error.message, "error");
  } finally {
    sendButton.disabled = false;
  }
}

function currentMkiiPlugin(catalog) {
  if (lastMkiiPluginId) {
    const remembered = catalog.plugins.find((entry) => entry.id === lastMkiiPluginId);
    if (remembered && mkiiBuild(remembered)) {
      return remembered;
    }
  }
  return catalog.plugins.find((entry) => mkiiBuild(entry)) || catalog.plugins[0];
}

async function main() {
  applySlotModule("osc");

  const webMidiSupported = Boolean(navigator.requestMIDIAccess);
  midiUnsupported.hidden = webMidiSupported;
  midiPanel.hidden = !webMidiSupported;
  if (!webMidiSupported) {
    setStatus("Use Chrome or Edge", "warn");
  } else {
    setStatus("Connect over USB-C", "idle");
  }

  let catalog;
  try {
    catalog = await loadCatalog();
  } catch (error) {
    log(error.message, "error");
    pluginListEl.innerHTML = `<p class="empty">${error.message}</p>`;
    return;
  }

  renderPlugins(catalog);

  connectButton.addEventListener("click", () => {
    connectMidi();
  });

  pluginListEl.addEventListener("click", async (event) => {
    const button = event.target.closest("[data-send]");
    if (!button) {
      return;
    }
    const plugin = catalog.plugins.find((entry) => entry.id === button.dataset.send);
    if (plugin) {
      const target = button.dataset.target || "nts-1_mkii";
      if (target === "nts-1_mkii") {
        lastMkiiPluginId = plugin.id;
      }
      await sendPlugin(plugin, target);
    }
  });

  sendButton.addEventListener("click", async () => {
    await sendPlugin(currentMkiiPlugin(catalog));
  });
}

main();
