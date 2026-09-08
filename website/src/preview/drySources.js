export const DRY_SOURCES = [
  { id: "house", labelKey: "sourceHouse" },
  { id: "techno", labelKey: "sourceTechno" },
  { id: "garage", labelKey: "sourceGarage" },
  { id: "acid", labelKey: "sourceAcid" },
  { id: "kick", labelKey: "sourceKick" },
  { id: "breakbeat", labelKey: "sourceBreakbeat" },
  { id: "reese", labelKey: "sourceReese" },
  { id: "stab", labelKey: "sourceStab" },
  { id: "sawtooth", labelKey: "sourceSawtooth" },
  { id: "square", labelKey: "sourceSquare" },
  { id: "sine", labelKey: "sourceSine" },
  { id: "triangle", labelKey: "sourceTriangle" },
  { id: "noise", labelKey: "sourceNoise" },
];

export const DRY_SOURCE_IDS = new Set(DRY_SOURCES.map((source) => source.id));

export function defaultDrySourceId(plugin) {
  if (plugin?.id === "technorumble" || plugin?.id === "pumpduck") {
    return "kick";
  }
  return "house";
}

export function isDrySourceId(sourceId) {
  return DRY_SOURCE_IDS.has(sourceId);
}
