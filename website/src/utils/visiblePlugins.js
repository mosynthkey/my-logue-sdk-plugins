export function isExperimentalEnabled(searchParams) {
  if (!searchParams) {
    return false;
  }
  if (!searchParams.has("experimental")) {
    return false;
  }
  const value = searchParams.get("experimental");
  if (value === null || value === "") {
    return true;
  }
  const normalized = value.trim().toLowerCase();
  return normalized !== "0" && normalized !== "false" && normalized !== "no";
}

export function visiblePlugins(plugins, searchParams) {
  const showExperimental = isExperimentalEnabled(searchParams);
  return (plugins || []).filter((plugin) => !plugin.experimental || showExperimental);
}

export function findPlugin(plugins, pluginId) {
  return (plugins || []).find((plugin) => plugin.id === pluginId) || null;
}

export function resolveInitialPlugin(plugins, searchParams) {
  const requestedPluginId = searchParams?.get("plugin")?.trim();
  if (requestedPluginId) {
    const requestedPlugin = findPlugin(plugins, requestedPluginId);
    if (requestedPlugin) {
      return requestedPlugin;
    }
  }

  const listedPlugins = visiblePlugins(plugins, searchParams);
  return listedPlugins[0] || null;
}
