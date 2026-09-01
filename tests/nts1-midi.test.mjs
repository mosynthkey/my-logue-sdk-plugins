import {
  crc32,
  hostToMidi,
  midiToHost,
  wrapUnitFile,
  buildUserSlotDataPackets,
  buildSysex,
  USER_SLOT_DATA,
  USER_SLOT_STATUS,
  parseIdentityReply,
  isInquiryReply,
  parseSlotStatusReply,
  decodeUnitName,
  listMidiPorts,
  portLabel,
  pickPreferredPort,
  NTS3_KAOSS,
} from "../website/nts1-midi.js";

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function arraysEqual(left, right) {
  if (left.length !== right.length) {
    return false;
  }
  for (let index = 0; index < left.length; index++) {
    if (left[index] !== right[index]) {
      return false;
    }
  }
  return true;
}

const roundtrip = [0x00, 0x80, 0xff, 0x01, 0x7f, 0xaa, 0x55, 0x10];
const packed = hostToMidi(roundtrip);
const unpacked = midiToHost(packed);
assert(arraysEqual(roundtrip, unpacked), "7-bit pack/unpack roundtrip failed");

const crcCheck = crc32(new TextEncoder().encode("123456789"));
assert(crcCheck === 0xcbf43926, `CRC-32 mismatch: 0x${crcCheck.toString(16)}`);

const dummyUnit = new Uint8Array([0x7f, 0x45, 0x4c, 0x46, 0x01, 0x02, 0x03]);
const wrapped = wrapUnitFile(dummyUnit);
assert(wrapped.length === dummyUnit.length + 8, "wrap length");
assert(wrapped[0] === dummyUnit.length, "little-endian length");

const packets = buildUserSlotDataPackets(dummyUnit, { module: "osc", slot: 3, channel: 1 });
assert(packets.length === 1, "small unit is a single packet");
assert(packets[0][0] === 0xf0 && packets[0][packets[0].length - 1] === 0xf7, "F0/F7 framing");
assert(packets[0][1] === 0x42 && packets[0][5] === 0x73, "KORG exclusive header");
assert(packets[0][6] === USER_SLOT_DATA, "USER SLOT DATA command");
assert(packets[0][7] === 4 && packets[0][8] === 3, "osc module id and slot");

const largeUnit = new Uint8Array(9000).map((_, index) => index & 0xff);
const largePackets = buildUserSlotDataPackets(largeUnit, { module: "osc", slot: 0 });
assert(largePackets.length > 1, "large unit is split across packets");
for (const packet of largePackets) {
  assert(packet.length <= 4096, `packet exceeds 4096 bytes: ${packet.length}`);
}

const identity = buildSysex(2, 0x17, []);
assert(identity[2] === 0x31, "channel 2 encodes as 0x31");

const inquiryReply13 = Uint8Array.from([
  0xf0, 0x7e, 0x00, 0x06, 0x02, 0x42, 0x73, 0x01, 0x34, 0x12, 0x02, 0x01, 0xf7,
]);
assert(isInquiryReply(inquiryReply13), "13-byte inquiry reply shape");
const parsed13 = parseIdentityReply(inquiryReply13);
assert(parsed13.modelNumber === 0x1234, "13-byte model number");
assert(parsed13.softwareVersion === 0x0102, "13-byte software version");

const inquiryReply = Uint8Array.from([
  0xf0, 0x7e, 0x00, 0x06, 0x02, 0x00, 0x00, 0x42, 0x73, 0x01, 0x34, 0x12, 0x02, 0x01, 0xf7,
]);
assert(isInquiryReply(inquiryReply), "extended manufacturer inquiry reply shape");
const parsed = parseIdentityReply(inquiryReply);
assert(parsed.modelNumber === 0x1234, "model number");
assert(parsed.softwareVersion === 0x0102, "software version");
assert(parsed.label.includes("NTS-1 mkII"), "device label");

const fakePortMap = new Map([
  ["port-a", { id: "port-a", name: "NTS-1 mkII MIDI OUT" }],
  ["port-b", { id: "port-b", name: "NTS-1 mkII MIDI IN" }],
]);
const listedPorts = listMidiPorts(fakePortMap);
assert(listedPorts.length === 2, "listMidiPorts reads Map values");
assert(listedPorts[0].name === "NTS-1 mkII MIDI OUT", "listMidiPorts keeps port objects");
assert(portLabel(listedPorts[0]).includes("NTS-1"), "portLabel uses port name");

function encodeUint32LE(value) {
  return [value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff];
}

const emptySlotReply = buildSysex(1, USER_SLOT_STATUS, [4, 2]);
const emptyStatus = parseSlotStatusReply(emptySlotReply);
assert(emptyStatus.empty, "empty slot has no packed header");
assert(emptyStatus.module === "osc" && emptyStatus.slot === 2, "empty slot module and index");

const unitName = "HyperSaw";
const nameBytes = new Uint8Array(20);
for (let charIndex = 0; charIndex < unitName.length; charIndex++) {
  nameBytes[charIndex] = unitName.charCodeAt(charIndex);
}
const headerBytes = [
  ...encodeUint32LE(32),
  ...encodeUint32LE(0),
  ...encodeUint32LE(0),
  ...encodeUint32LE(0),
  ...encodeUint32LE(4),
  ...encodeUint32LE(0x00010000),
  ...nameBytes,
];
const occupiedSlotReply = buildSysex(1, USER_SLOT_STATUS, [4, 1, 0, ...hostToMidi(headerBytes)]);
const occupiedStatus = parseSlotStatusReply(occupiedSlotReply);
assert(!occupiedStatus.empty, "occupied slot is not empty");
assert(occupiedStatus.slot === 1, "occupied slot index");
assert(occupiedStatus.name === "HyperSaw", `occupied slot name: ${occupiedStatus.name}`);
assert(decodeUnitName(nameBytes) === "HyperSaw", "decodeUnitName strips padding");

const nts3Packets = buildUserSlotDataPackets(dummyUnit, {
  module: "genericfx",
  slot: 12,
  channel: 1,
  device: NTS3_KAOSS,
});
assert(nts3Packets.length === 1, "NTS-3 small unit is a single packet");
assert(nts3Packets[0][1] === 0x42 && nts3Packets[0][5] === 0x72, "NTS-3 exclusive header family 0x72");
assert(nts3Packets[0][6] === USER_SLOT_DATA, "NTS-3 USER SLOT DATA command");
assert(nts3Packets[0][7] === 7 && nts3Packets[0][8] === 12, "genericfx module id and slot");

let nts3SlotRejected = false;
try {
  buildUserSlotDataPackets(dummyUnit, { module: "genericfx", slot: 50, device: NTS3_KAOSS });
} catch (error) {
  nts3SlotRejected = /out of range/.test(error.message);
}
assert(nts3SlotRejected, "NTS-3 genericfx rejects slot 50");

const nts3Inquiry = Uint8Array.from([
  0xf0, 0x7e, 0x00, 0x06, 0x02, 0x42, 0x72, 0x01, 0x01, 0x00, 0x00, 0x01, 0xf7,
]);
assert(isInquiryReply(nts3Inquiry), "NTS-3 inquiry reply shape");
const nts3Identity = parseIdentityReply(nts3Inquiry);
assert(nts3Identity.deviceId === "nts-3_kaoss", "NTS-3 device id");
assert(nts3Identity.label.includes("NTS-3"), "NTS-3 device label");

const mixedPorts = new Map([
  ["port-a", { id: "port-a", name: "NTS-1 mkII MIDI OUT" }],
  ["port-b", { id: "port-b", name: "NTS-3 kaoss pad kit" }],
  ["port-c", { id: "port-c", name: "NTS-3 kaoss pad kit" }],
]);
const preferredNts3 = pickPreferredPort(mixedPorts, NTS3_KAOSS);
assert(preferredNts3.id === "port-c", "NTS-3 prefers the last matching port");

console.log("nts1-midi tests passed");
