import { computed, ref, shallowRef } from "vue";
import { filterPluginsByCategory, pluginCategory } from "../utils/pluginCategory.js";
import { buildForTarget, defaultTarget } from "../utils/plugin.js";
import { findPlugin, resolveInitialPlugin, visiblePlugins } from "../utils/visiblePlugins.js";

export function usePluginSelection(catalog, siteQuery) {
  const selectedPluginId = ref(null);
  const selectedTargetByPlugin = ref(new Map());
  const activePlugin = shallowRef(null);
  const activeTarget = ref("nts-1_mkii");
  const selectedCategory = computed(() => siteQuery.requestedCategory());

  function listedPluginsFor(category = selectedCategory.value) {
    const plugins = catalog.value?.plugins || [];
    const listedPlugins = visiblePlugins(plugins, siteQuery.searchParams.value);
    return filterPluginsByCategory(listedPlugins, category);
  }

  const sidebarPlugins = computed(() => {
    const listedPlugins = visiblePlugins(catalog.value?.plugins || [], siteQuery.searchParams.value);
    const filteredPlugins = listedPluginsFor();
    const active = activePlugin.value;

    if (
      active?.experimental
      && !listedPlugins.some((plugin) => plugin.id === active.id)
      && (selectedCategory.value === "all" || pluginCategory(active) === selectedCategory.value)
    ) {
      return [...filteredPlugins, active].sort((left, right) => left.name.localeCompare(right.name));
    }

    return filteredPlugins;
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

  function selectCategory(category) {
    siteQuery.syncCategory(category);
    const filteredPlugins = listedPluginsFor(category);
    if (filteredPlugins.some((plugin) => plugin.id === selectedPluginId.value)) {
      return;
    }
    if (filteredPlugins[0]) {
      selectPlugin(filteredPlugins[0].id);
    }
  }

  function initializeSelection() {
    const plugins = catalog.value?.plugins || [];
    if (plugins.length === 0) {
      return;
    }

    const requestedPluginId = siteQuery.requestedPluginId();
    let initialPlugin = resolveInitialPlugin(plugins, siteQuery.searchParams.value);
    if (!requestedPluginId) {
      initialPlugin = listedPluginsFor()[0] || initialPlugin;
    }
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
    selectedCategory,
    activePlugin,
    activeTarget,
    sidebarPlugins,
    selectPlugin,
    selectTarget,
    selectCategory,
    initializeSelection,
  };
}
