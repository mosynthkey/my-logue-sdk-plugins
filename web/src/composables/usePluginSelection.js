import { ref, shallowRef } from "vue";
import { buildForTarget, defaultTarget } from "../utils/plugin.js";

export function usePluginSelection(catalog) {
  const selectedPluginId = ref(null);
  const selectedTargetByPlugin = ref(new Map());
  const activePlugin = shallowRef(null);
  const activeTarget = ref("nts-1_mkii");

  async function selectTarget(pluginId, target) {
    const nextTargets = new Map(selectedTargetByPlugin.value);
    nextTargets.set(pluginId, target);
    selectedTargetByPlugin.value = nextTargets;

    const plugin = catalog.value?.plugins.find((entry) => entry.id === pluginId);
    if (!plugin) {
      return;
    }

    activePlugin.value = plugin;
    activeTarget.value = target;
  }

  async function selectPlugin(pluginId) {
    const plugin = catalog.value?.plugins.find((entry) => entry.id === pluginId);
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
  }

  function initializeSelection() {
    const plugins = catalog.value?.plugins || [];
    if (plugins.length === 0) {
      return;
    }

    const initialPluginId = selectedPluginId.value || plugins[0].id;
    selectPlugin(initialPluginId);
  }

  return {
    selectedPluginId,
    selectedTargetByPlugin,
    activePlugin,
    activeTarget,
    selectPlugin,
    selectTarget,
    initializeSelection,
  };
}
