import { downloadableBuilds, unitFileName } from "./plugin.js";
import { createStoreZip } from "./zipStore.js";

export const UNITS_ZIP_FILENAME = "logue-sdk-units.zip";

export function zipFolderName(plugin) {
  const raw = String(plugin?.name || plugin?.id || "plugin");
  const sanitized = raw.replace(/[\\/:*?"<>|]/g, "_").trim();
  return sanitized || plugin.id || "plugin";
}

export function unitZipEntries(plugins) {
  const entries = [];
  for (const plugin of plugins || []) {
    const folder = zipFolderName(plugin);
    for (const build of downloadableBuilds(plugin)) {
      entries.push({
        path: `${folder}/${unitFileName(build)}`,
        url: build.file,
      });
    }
  }
  return entries;
}

export async function buildUnitsZipBytes(plugins, fetchImpl = fetch) {
  const entries = unitZipEntries(plugins);
  if (entries.length === 0) {
    throw new Error("No downloadable units");
  }

  const files = [];
  for (const entry of entries) {
    const response = await fetchImpl(entry.url);
    if (!response?.ok) {
      throw new Error(`Failed to fetch ${entry.url}`);
    }
    files.push({
      name: entry.path,
      data: new Uint8Array(await response.arrayBuffer()),
    });
  }
  return createStoreZip(files);
}

export function triggerZipDownload(zipBytes, filename = UNITS_ZIP_FILENAME, documentRef = document) {
  const blob = new Blob([zipBytes], { type: "application/zip" });
  const objectUrl = URL.createObjectURL(blob);
  const link = documentRef.createElement("a");
  link.href = objectUrl;
  link.download = filename;
  documentRef.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(objectUrl);
}

export async function downloadVisibleUnitsZip(plugins, fetchImpl = fetch, documentRef = document) {
  const zipBytes = await buildUnitsZipBytes(plugins, fetchImpl);
  triggerZipDownload(zipBytes, UNITS_ZIP_FILENAME, documentRef);
  return zipBytes;
}
