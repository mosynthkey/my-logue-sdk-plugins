export const PLUGIN_CATEGORIES = ["oscillator", "synth", "drum", "fx"];

const TYPE_TO_CATEGORY = {
  osc: "oscillator",
  loopkey: "oscillator",
  synth: "synth",
  fm: "synth",
  drum: "drum",
  shaker: "drum",
  fx: "fx",
};

const CATEGORY_MESSAGE_KEYS = {
  all: "categoryAll",
  oscillator: "categoryOscillator",
  synth: "categorySynth",
  drum: "categoryDrum",
  fx: "categoryFx",
};

export function pluginCategory(plugin) {
  if (plugin?.category && PLUGIN_CATEGORIES.includes(plugin.category)) {
    return plugin.category;
  }
  return TYPE_TO_CATEGORY[plugin?.type] || "synth";
}

export function filterPluginsByCategory(plugins, category) {
  if (!category || category === "all") {
    return plugins || [];
  }
  return (plugins || []).filter((plugin) => pluginCategory(plugin) === category);
}

export function normalizeCategory(category) {
  if (PLUGIN_CATEGORIES.includes(category)) {
    return category;
  }
  return "all";
}

export function categoryMessageKey(category) {
  return CATEGORY_MESSAGE_KEYS[category] || null;
}
