import { SENDABLE_TARGETS, TARGET_LABEL } from "../constants.js";

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

export function moduleFor(plugin, target) {
  const build = buildForTarget(plugin, target);
  return (build && build.module) || plugin.module || "osc";
}

export function defaultTarget(plugin, selectedTargetByPlugin) {
  const builds = sendableBuilds(plugin);
  return (
    selectedTargetByPlugin.get(plugin.id)
    || builds.find((build) => build.target === "nts-1_mkii")?.target
    || builds[0]?.target
    || "nts-1_mkii"
  );
}
