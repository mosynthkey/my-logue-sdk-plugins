const CRC_TABLE = new Uint32Array(256);

for (let tableIndex = 0; tableIndex < 256; ++tableIndex) {
  let crc = tableIndex;
  for (let bitIndex = 0; bitIndex < 8; ++bitIndex) {
    crc = (crc & 1) !== 0 ? (0xedb88320 ^ (crc >>> 1)) : (crc >>> 1);
  }
  CRC_TABLE[tableIndex] = crc >>> 0;
}

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (let byteIndex = 0; byteIndex < bytes.length; ++byteIndex) {
    crc = CRC_TABLE[(crc ^ bytes[byteIndex]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function writeUtf8(text) {
  return new TextEncoder().encode(text);
}

function writeU16(view, offset, value) {
  view.setUint16(offset, value, true);
}

function writeU32(view, offset, value) {
  view.setUint32(offset, value, true);
}

export function createStoreZip(files) {
  const localChunks = [];
  const centralChunks = [];
  let localOffset = 0;

  for (const file of files) {
    const nameBytes = writeUtf8(file.name);
    const data = file.data;
    const checksum = crc32(data);
    const local = new Uint8Array(30 + nameBytes.length);
    const localView = new DataView(local.buffer);
    writeU32(localView, 0, 0x04034b50);
    writeU16(localView, 4, 20);
    writeU16(localView, 8, 0);
    writeU16(localView, 10, 0);
    writeU16(localView, 12, 0);
    writeU32(localView, 14, checksum);
    writeU32(localView, 18, data.length);
    writeU32(localView, 22, data.length);
    writeU16(localView, 26, nameBytes.length);
    local.set(nameBytes, 30);

    const central = new Uint8Array(46 + nameBytes.length);
    const centralView = new DataView(central.buffer);
    writeU32(centralView, 0, 0x02014b50);
    writeU16(centralView, 4, 20);
    writeU16(centralView, 6, 20);
    writeU16(centralView, 10, 0);
    writeU16(centralView, 12, 0);
    writeU16(centralView, 14, 0);
    writeU32(centralView, 16, checksum);
    writeU32(centralView, 20, data.length);
    writeU32(centralView, 24, data.length);
    writeU16(centralView, 28, nameBytes.length);
    writeU32(centralView, 42, localOffset);
    central.set(nameBytes, 46);

    localChunks.push(local, data);
    centralChunks.push(central);
    localOffset += local.length + data.length;
  }

  const centralSize = centralChunks.reduce((total, chunk) => total + chunk.length, 0);
  const eocd = new Uint8Array(22);
  const eocdView = new DataView(eocd.buffer);
  writeU32(eocdView, 0, 0x06054b50);
  writeU16(eocdView, 8, files.length);
  writeU16(eocdView, 10, files.length);
  writeU32(eocdView, 12, centralSize);
  writeU32(eocdView, 16, localOffset);

  const totalSize = localOffset + centralSize + eocd.length;
  const zip = new Uint8Array(totalSize);
  let writeOffset = 0;
  for (const chunk of [...localChunks, ...centralChunks, eocd]) {
    zip.set(chunk, writeOffset);
    writeOffset += chunk.length;
  }
  return zip;
}

export function listStoreZipPaths(zipBytes) {
  const view = new DataView(zipBytes.buffer, zipBytes.byteOffset, zipBytes.byteLength);
  const decoder = new TextDecoder();
  const paths = [];
  let offset = 0;

  while (offset + 30 <= zipBytes.length) {
    const signature = view.getUint32(offset, true);
    if (signature !== 0x04034b50) {
      break;
    }
    const nameLength = view.getUint16(offset + 26, true);
    const extraLength = view.getUint16(offset + 28, true);
    const dataLength = view.getUint32(offset + 22, true);
    const nameStart = offset + 30;
    paths.push(decoder.decode(zipBytes.subarray(nameStart, nameStart + nameLength)));
    offset = nameStart + nameLength + extraLength + dataLength;
  }

  return paths;
}
