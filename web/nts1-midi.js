/**
 * NTS-1 mkII user-unit transfer via Web MIDI SysEx.
 *
 * Message layout follows the public KORG document
 * "NTS-1 digital kit mkII MIDI Implementation" (2024.04.02):
 *   F0 42 3g 00 01 73 <id> <payload...> F7
 * USER SLOT DATA (4AH) carries 7-bit packed host data (NOTE 1).
 * Payload (8-bit, before packing): little-endian length, CRC-32, then the .nts1mkiiunit ELF.
 */

export const KORG_ID = 0x42;
export const FAMILY_LSB = 0x73;
export const FAMILY_MSB = 0x01;
export const USER_SLOT_DATA = 0x4a;
export const USER_SLOT_STATUS_REQUEST = 0x19;
export const USER_SLOT_STATUS = 0x49;
export const USER_API_VERSION_REQUEST = 0x17;
export const USER_API_VERSION = 0x47;
export const CLEAR_USER_SLOT = 0x1b;

export const STATUS = {
  0x23: "Operation completed",
  0x24: "Operation error",
  0x26: "Data format error",
  0x27: "User data size error",
  0x28: "User data CRC error",
  0x29: "User target error",
  0x2a: "User API error",
  0x2b: "User load size error",
  0x2c: "User module error",
  0x2d: "User slot error",
  0x2e: "User format error",
  0x2f: "User internal error",
};

export const MODULE_IDS = { modfx: 1, delfx: 2, revfx: 3, osc: 4 };
export const MODULE_SLOTS = { modfx: 16, osc: 16, delfx: 8, revfx: 8 };
export const UNIT_NAME_SIZE = 20;
export const UNIT_HEADER_NAME_OFFSET = 24;

// Exclusive message must stay within 4096 bytes including F0/F7.
const MAX_MSG_SIZE = 4096;
const SYSEX_OVERHEAD = 12; // F0 + header(5) + id + module + slot + seq + seqMax + F7
const MAX_MIDI_DATA_SIZE = MAX_MSG_SIZE - SYSEX_OVERHEAD;
export const MAX_HOST_DATA_SIZE = 3573; // 7 host bytes pack into 8 MIDI bytes

const CRC32_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let tableIndex = 0; tableIndex < 256; tableIndex++) {
    let entry = tableIndex;
    for (let bitIndex = 0; bitIndex < 8; bitIndex++) {
      entry = entry & 1 ? (entry >>> 1) ^ 0xedb88320 : entry >>> 1;
    }
    table[tableIndex] = entry >>> 0;
  }
  return table;
})();

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (let byteIndex = 0; byteIndex < bytes.length; byteIndex++) {
    crc = CRC32_TABLE[(crc ^ bytes[byteIndex]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/** KORG NOTE 1: 8-bit host bytes -> 7-bit MIDI bytes (7 host -> 8 MIDI). */
export function hostToMidi(hostBytes) {
  const midiBytes = [];
  for (let blockStart = 0; blockStart < hostBytes.length; blockStart += 7) {
    const blockEnd = Math.min(blockStart + 7, hostBytes.length);
    let msbits = 0;
    const stored = [];
    for (let byteIndex = blockStart; byteIndex < blockEnd; byteIndex++) {
      const value = hostBytes[byteIndex];
      const bitOffset = byteIndex - blockStart;
      msbits |= (value & 0x80) >> (7 - bitOffset);
      stored.push(value & 0x7f);
    }
    midiBytes.push(msbits, ...stored);
  }
  return midiBytes;
}

export function midiToHost(midiBytes) {
  const hostBytes = [];
  for (let blockStart = 0; blockStart < midiBytes.length; blockStart += 8) {
    const msbits = midiBytes[blockStart];
    const blockEnd = Math.min(blockStart + 8, midiBytes.length);
    for (let midiIndex = blockStart + 1; midiIndex < blockEnd; midiIndex++) {
      const bitOffset = midiIndex - blockStart - 1;
      const highBit = (msbits & (1 << bitOffset)) << (7 - bitOffset);
      hostBytes.push(midiBytes[midiIndex] | highBit);
    }
  }
  return hostBytes;
}

export function exclusiveHeader(channel) {
  const channelNibble = Math.max(0, Math.min(15, (channel || 1) - 1));
  return [KORG_ID, 0x30 | channelNibble, 0x00, 0x01, FAMILY_LSB];
}

export function isNts1Mk2Exclusive(data) {
  return (
    data.length >= 6 &&
    data[0] === 0xf0 &&
    data[1] === KORG_ID &&
    (data[2] & 0xf0) === 0x30 &&
    data[3] === 0x00 &&
    data[4] === 0x01 &&
    data[5] === FAMILY_LSB
  );
}

function identityFamilyOffset(data) {
  if (
    data.length >= 15 &&
    data[5] === 0x00 &&
    data[6] === 0x00 &&
    data[7] === KORG_ID &&
    data[8] === FAMILY_LSB &&
    data[9] === FAMILY_MSB
  ) {
    return 8;
  }
  if (data.length >= 13 && data[5] === KORG_ID && data[6] === FAMILY_LSB && data[7] === FAMILY_MSB) {
    return 6;
  }
  return -1;
}

export function isInquiryReply(data) {
  return (
    data.length >= 13 &&
    data[0] === 0xf0 &&
    data[1] === 0x7e &&
    data[3] === 0x06 &&
    data[4] === 0x02 &&
    data[data.length - 1] === 0xf7 &&
    identityFamilyOffset(data) >= 0
  );
}

export function parseIdentityReply(data) {
  const familyOffset = identityFamilyOffset(data);
  if (familyOffset < 0) {
    return null;
  }
  const modelOffset = familyOffset + 2;
  if (modelOffset + 3 >= data.length) {
    return null;
  }
  const modelNumber = data[modelOffset] | (data[modelOffset + 1] << 8);
  const softwareVersion = data[modelOffset + 2] | (data[modelOffset + 3] << 8);
  return {
    manufacturer: "KORG",
    family: "NTS-1 digital kit mkII",
    modelNumber,
    softwareVersion,
    label: `NTS-1 mkII · model ${modelNumber} · v${softwareVersion >> 8}.${softwareVersion & 0xff}`,
    raw: data,
  };
}

export function buildSysex(channel, commandId, payload = []) {
  return Uint8Array.from([0xf0, ...exclusiveHeader(channel), commandId, ...payload, 0xf7]);
}

export function wrapUnitFile(unitBytes) {
  const source = unitBytes instanceof Uint8Array ? unitBytes : new Uint8Array(unitBytes);
  const wrapped = new Uint8Array(8 + source.length);
  const length = source.length;
  const checksum = crc32(source);
  wrapped[0] = length & 0xff;
  wrapped[1] = (length >>> 8) & 0xff;
  wrapped[2] = (length >>> 16) & 0xff;
  wrapped[3] = (length >>> 24) & 0xff;
  wrapped[4] = checksum & 0xff;
  wrapped[5] = (checksum >>> 8) & 0xff;
  wrapped[6] = (checksum >>> 16) & 0xff;
  wrapped[7] = (checksum >>> 24) & 0xff;
  wrapped.set(source, 8);
  return wrapped;
}

export function buildUserSlotDataPackets(unitBytes, { module = "osc", slot = 0, channel = 1 } = {}) {
  const moduleId = MODULE_IDS[module];
  if (moduleId === undefined) {
    throw new Error(`Unknown module "${module}"`);
  }
  const slotCount = MODULE_SLOTS[module];
  if (slot < 0 || slot >= slotCount) {
    throw new Error(`Slot ${slot} is out of range for ${module} (0-${slotCount - 1})`);
  }

  const programData = wrapUnitFile(unitBytes);
  const sequenceMax = Math.max(0, Math.ceil(programData.length / MAX_HOST_DATA_SIZE) - 1);
  const packets = [];

  for (let sequenceNum = 0; sequenceNum <= sequenceMax; sequenceNum++) {
    const chunkStart = sequenceNum * MAX_HOST_DATA_SIZE;
    const chunkEnd = Math.min(chunkStart + MAX_HOST_DATA_SIZE, programData.length);
    const chunk = programData.subarray(chunkStart, chunkEnd);
    const payload = [moduleId, slot, sequenceNum, sequenceMax, ...hostToMidi(chunk)];
    const message = buildSysex(channel, USER_SLOT_DATA, payload);
    if (message.length > MAX_MSG_SIZE) {
      throw new Error(`SysEx packet ${sequenceNum} is ${message.length} bytes (max ${MAX_MSG_SIZE})`);
    }
    packets.push(message);
  }

  return packets;
}

function commandIdFrom(data) {
  return isNts1Mk2Exclusive(data) ? data[6] : null;
}

export function describeStatus(data) {
  const commandId = commandIdFrom(data);
  if (commandId === null) {
    return null;
  }
  if (STATUS[commandId]) {
    return { id: commandId, ok: commandId === 0x23, message: STATUS[commandId] };
  }
  return { id: commandId, ok: false, message: `Unexpected SysEx 0x${commandId.toString(16)}` };
}

function waitForSysex(input, predicate, timeoutMs) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      input.removeEventListener("midimessage", onMessage);
      reject(new Error("Timed out waiting for NTS-1 mkII SysEx reply"));
    }, timeoutMs);

    function onMessage(event) {
      const data = event.data instanceof Uint8Array ? event.data : new Uint8Array(event.data);
      if (data.length === 0 || data[0] !== 0xf0) {
        return;
      }
      if (!predicate(data)) {
        return;
      }
      clearTimeout(timer);
      input.removeEventListener("midimessage", onMessage);
      resolve(data);
    }

    input.addEventListener("midimessage", onMessage);
  });
}

export async function requestIdentity(output, input, { channel = 1, timeoutMs = 1500 } = {}) {
  const identityRequest = Uint8Array.from([0xf0, 0x7e, channel - 1, 0x06, 0x01, 0xf7]);
  const pending = waitForSysex(input, isInquiryReply, timeoutMs);
  output.send(identityRequest);
  const reply = await pending;
  const identity = parseIdentityReply(reply);
  if (!identity) {
    throw new Error("Unrecognized device identity reply");
  }
  return identity;
}

export async function detectDevice(output, input, { channel = 1, timeoutMs = 1500 } = {}) {
  if (!output || !input) {
    throw new Error("Select MIDI ports");
  }
  return requestIdentity(output, input, { channel, timeoutMs });
}

function moduleNameFromId(moduleId) {
  for (const [name, id] of Object.entries(MODULE_IDS)) {
    if (id === moduleId) {
      return name;
    }
  }
  return String(moduleId);
}

function readUint32LE(bytes, offset) {
  return (
    (bytes[offset] |
      (bytes[offset + 1] << 8) |
      (bytes[offset + 2] << 16) |
      (bytes[offset + 3] << 24)) >>>
    0
  );
}

export function decodeUnitName(bytes) {
  const chars = [];
  const source = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
  for (let byteIndex = 0; byteIndex < source.length; byteIndex++) {
    const code = source[byteIndex];
    if (code === 0) {
      break;
    }
    if (code >= 32 && code < 127) {
      chars.push(String.fromCharCode(code));
    }
  }
  return chars.join("").trim();
}

export function parseSlotStatusReply(data) {
  if (commandIdFrom(data) !== USER_SLOT_STATUS) {
    throw new Error("Not a USER SLOT STATUS reply");
  }
  const payload = Array.from(data.slice(7, data.length - 1));
  const moduleId = payload[0];
  const slot = payload[1];
  const module = moduleNameFromId(moduleId);
  if (payload.length <= 2) {
    return { module, slot, empty: true, name: "", raw: payload };
  }

  const headerBytes = midiToHost(payload.slice(3));
  const nameStart = UNIT_HEADER_NAME_OFFSET;
  const nameEnd = nameStart + UNIT_NAME_SIZE;
  const name =
    headerBytes.length >= nameEnd ? decodeUnitName(headerBytes.slice(nameStart, nameEnd)) : "";
  const headerSize = headerBytes.length >= 4 ? readUint32LE(headerBytes, 0) : 0;
  return {
    module,
    slot,
    empty: false,
    name,
    headerSize,
    raw: payload,
  };
}

export async function readSlotStatus(output, input, { module = "osc", slot = 0, channel = 1, timeoutMs = 2000 } = {}) {
  const moduleId = MODULE_IDS[module];
  if (moduleId === undefined) {
    throw new Error(`Unknown module "${module}"`);
  }
  const request = buildSysex(channel, USER_SLOT_STATUS_REQUEST, [moduleId, slot]);
  const pending = waitForSysex(
    input,
    (data) => {
      if (commandIdFrom(data) !== USER_SLOT_STATUS) {
        return false;
      }
      const payload = data.slice(7, data.length - 1);
      return payload.length >= 2 && payload[0] === moduleId && payload[1] === slot;
    },
    timeoutMs,
  );
  output.send(request);
  return parseSlotStatusReply(await pending);
}

export async function readModuleSlots(output, input, { module = "osc", channel = 1, timeoutMs = 800 } = {}) {
  const slotCount = MODULE_SLOTS[module] || 0;
  const slots = [];
  for (let slotIndex = 0; slotIndex < slotCount; slotIndex++) {
    slots.push(await readSlotStatus(output, input, { module, slot: slotIndex, channel, timeoutMs }));
  }
  return slots;
}

export async function installUnit(output, input, unitBytes, options = {}) {
  const {
    module = "osc",
    slot = 0,
    channel = 1,
    timeoutMs = 8000,
    onProgress = () => {},
  } = options;

  const packets = buildUserSlotDataPackets(unitBytes, { module, slot, channel });
  onProgress({ phase: "start", packetIndex: 0, packetCount: packets.length });

  for (let packetIndex = 0; packetIndex < packets.length; packetIndex++) {
    const pending = waitForSysex(
      input,
      (data) => commandIdFrom(data) !== null && STATUS[commandIdFrom(data)] !== undefined,
      timeoutMs,
    );
    output.send(packets[packetIndex]);
    const reply = await pending;
    const status = describeStatus(reply);
    onProgress({
      phase: "packet",
      packetIndex: packetIndex + 1,
      packetCount: packets.length,
      status,
    });
    if (!status.ok) {
      throw new Error(`NTS-1 mkII rejected packet ${packetIndex + 1}/${packets.length}: ${status.message}`);
    }
  }

  onProgress({ phase: "done", packetIndex: packets.length, packetCount: packets.length });
  return { packetCount: packets.length };
}

export function looksLikeNts1Name(name) {
  return /nts-?1/i.test(name || "");
}

export function listMidiPorts(ports) {
  if (ports && typeof ports.values === "function") {
    return Array.from(ports.values());
  }
  return Array.from(ports || []);
}

export function portLabel(port) {
  if (!port) {
    return "Unknown MIDI port";
  }
  return port.name || port.id || "Unknown MIDI port";
}

export function pickPreferredPort(ports) {
  const listed = listMidiPorts(ports);
  const ntsPorts = listed.filter((port) => looksLikeNts1Name(port.name));
  if (ntsPorts.length >= 2) {
    return ntsPorts[ntsPorts.length - 1];
  }
  if (ntsPorts.length === 1) {
    return ntsPorts[0];
  }
  return listed[0] || null;
}
