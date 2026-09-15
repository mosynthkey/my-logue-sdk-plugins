import { SENDABLE_TARGETS, TARGET_LABEL } from "../constants.js";

const KEYBOARD_PREVIEW_TARGETS = new Set(["nts-1_mkii", "microkorg2"]);

export function targetName(target) {
  return TARGET_LABEL[target] || target;
}

export function sendLabel(target) {
  return `Send to ${targetName(target)}`;
}

export function sendableBuilds(plugin) {
  return (plugin.builds || []).filter((build) => SENDABLE_TARGETS.has(build.target));
}

export function downloadableBuilds(plugin) {
  return (plugin.builds || []).filter((build) => typeof build.file === "string" && build.file.length > 0);
}

export function unitFileName(build) {
  if (!build?.file) return "";
  return build.file.split("/").pop() || build.file;
}

export function buildForTarget(plugin, target) {
  return (plugin.builds || []).find((build) => build.target === target) || null;
}

/** microKORG2 has no WASM; keyboard preview always uses the NTS-1 mkII build. */
export function canonicalPreviewTarget(target) {
  if (target === "nts-3_kaoss") {
    return "nts-3_kaoss";
  }
  if (KEYBOARD_PREVIEW_TARGETS.has(target)) {
    return "nts-1_mkii";
  }
  return target;
}

export function previewBuildForTarget(plugin, target) {
  const canonical = canonicalPreviewTarget(target);
  if (canonical === "nts-1_mkii") {
    return buildForTarget(plugin, "nts-1_mkii") || buildForTarget(plugin, target);
  }
  return buildForTarget(plugin, canonical);
}

export function previewModeItems(plugin) {
  const targets = new Set([
    ...(plugin.builds || []).map((build) => build.target),
    ...(plugin.targets || []),
  ]);
  const items = [];
  if ([...targets].some((target) => KEYBOARD_PREVIEW_TARGETS.has(target))) {
    items.push({ value: "nts-1_mkii", mode: "keyboard" });
  }
  if (targets.has("nts-3_kaoss")) {
    items.push({ value: "nts-3_kaoss", mode: "xypad" });
  }
  return items;
}

export function moduleFor(plugin, target) {
  const build = buildForTarget(plugin, target);
  return (build && build.module) || plugin.module || "osc";
}

export function defaultTarget(plugin, selectedTargetByPlugin) {
  const builds = sendableBuilds(plugin);
  const selected = selectedTargetByPlugin.get(plugin.id);
  if (selected) {
    return canonicalPreviewTarget(selected);
  }
  return (
    builds.find((build) => build.target === "nts-1_mkii")?.target
    || builds[0]?.target
    || "nts-1_mkii"
  );
}
