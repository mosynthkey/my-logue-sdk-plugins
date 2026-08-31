import { SELF_CONTAINED_TYPES } from "./constants.js";

export function previewLayout(build) {
  if (build?.target === "nts-3_kaoss") {
    return "xypad";
  }
  return "keyboard";
}

export function usesDryInput(plugin, build) {
  return build?.target === "nts-3_kaoss" && !SELF_CONTAINED_TYPES.has(plugin?.type);
}
