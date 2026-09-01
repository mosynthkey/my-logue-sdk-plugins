import { computed, ref, shallowRef } from "vue";
import { buildForTarget, defaultTarget } from "../utils/plugin.js";
import { findPlugin, resolveInitialPlugin, visiblePlugins } from "../utils/visiblePlugins.js";

export function usePluginSelection(catalog, siteQuery) {
  const selectedPluginId = ref(null);
  const selectedTargetByPlugin = ref(new Map());
  const activePlugin = shallowRef(null);
  const activeTarget = ref("nts-1_mkii");

  const sidebarPlugins = computed(() => {
    const plugins = catalog.value?.plugins || [];
    const listedPlugins = visiblePlugins(plugins, siteQuery.searchParams.value);
    const active = activePlugin.value;

    if (active?.experimental && !listedPlugins.some((plugin) => plugin.id === active.id)) {
      return [...listedPlugins, active].sort((left, right) => left.name.localeCompare(right.name));
    }

    return listedPlugins;
  });

  function syncUrl(pluginId, target) {
    siteQuery.syncSelection(pluginId, target);
  }

  async function selectTarget(pluginId, target) {
    const nextTargets = new Map(selectedTargetByPlugin.value);
    nextTargets.set(pluginId, target);
    selectedTargetByPlugin.value = nextTargets;

    const plugin = findPlugin(catalog.value?.plugins, pluginId);
    if (!plugin) {
      return;
    }

    activePlugin.value = plugin;
    activeTarget.value = target;
    syncUrl(pluginId, target);
  }

  async function selectPlugin(pluginId) {
    const plugin = findPlugin(catalog.value?.plugins, pluginId);
    if (!plugin) {
      return;
    }

    selectedPluginId.value = pluginId;
    activePlugin.value = plugin;

    const target = defaultTarget(plugin, selectedTargetByPlugin.value);
    const nextTargets = new Map(selectedTargetByPlugin.value);
    nextTargets.set(pluginId, target);
    selectedTargetByPlugin.value = nextTargets;
    activeTarget.value = target;
    syncUrl(pluginId, target);
  }

  function initializeSelection() {
    const plugins = catalog.value?.plugins || [];
    if (plugins.length === 0) {
      return;
    }

    const initialPlugin = resolveInitialPlugin(plugins, siteQuery.searchParams.value);
    if (!initialPlugin) {
      return;
    }

    const requestedTarget = siteQuery.requestedTarget();
    const target = requestedTarget && buildForTarget(initialPlugin, requestedTarget)
      ? requestedTarget
      : defaultTarget(initialPlugin, selectedTargetByPlugin.value);

    selectedPluginId.value = initialPlugin.id;
    activePlugin.value = initialPlugin;
    activeTarget.value = target;

    const nextTargets = new Map(selectedTargetByPlugin.value);
    nextTargets.set(initialPlugin.id, target);
    selectedTargetByPlugin.value = nextTargets;
    syncUrl(initialPlugin.id, target);
  }

  return {
    selectedPluginId,
    selectedTargetByPlugin,
    activePlugin,
    activeTarget,
    sidebarPlugins,
    selectPlugin,
    selectTarget,
    initializeSelection,
  };
}
