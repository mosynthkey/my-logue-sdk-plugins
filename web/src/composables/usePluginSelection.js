import { onBeforeUnmount, ref, shallowRef } from "vue";
import { mountPreview, teardownPreview } from "../../preview.js";
import { buildForTarget, defaultTarget } from "../utils/plugin.js";

export function usePluginSelection(catalog) {
  const selectedPluginId = ref(null);
  const selectedTargetByPlugin = ref(new Map());
  const activePlugin = shallowRef(null);
  const activeTarget = ref("nts-1_mkii");

  let previewMountChain = Promise.resolve();

  function enqueuePreviewMount(task) {
    previewMountChain = previewMountChain.then(task).catch(() => {});
    return previewMountChain;
  }

  async function mountActivePreview() {
    const plugin = activePlugin.value;
    if (!plugin) {
      return;
    }

    const build = buildForTarget(plugin, activeTarget.value);
    await enqueuePreviewMount(() => mountPreview(build, plugin));
  }

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
    await mountActivePreview();
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

    await mountActivePreview();
  }

  function initializeSelection() {
    const plugins = catalog.value?.plugins || [];
    if (plugins.length === 0) {
      return;
    }

    const initialPluginId = selectedPluginId.value || plugins[0].id;
    selectPlugin(initialPluginId);
  }

  onBeforeUnmount(() => {
    teardownPreview();
  });

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
