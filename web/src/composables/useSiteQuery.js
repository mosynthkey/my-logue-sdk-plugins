import { computed, ref } from "vue";
import { isExperimentalEnabled } from "../utils/visiblePlugins.js";

function readSearchParams() {
  if (typeof window === "undefined") {
    return new URLSearchParams();
  }
  return new URLSearchParams(window.location.search);
}

function writeSearchParams(nextParams) {
  if (typeof window === "undefined") {
    return;
  }

  const query = nextParams.toString();
  const nextUrl = query
    ? `${window.location.pathname}?${query}${window.location.hash}`
    : `${window.location.pathname}${window.location.hash}`;
  window.history.replaceState(null, "", nextUrl);
}

export function useSiteQuery() {
  const searchParams = ref(readSearchParams());

  const showExperimental = computed(() => isExperimentalEnabled(searchParams.value));

  function syncSelection(pluginId, target) {
    const nextParams = new URLSearchParams(searchParams.value);

    if (pluginId) {
      nextParams.set("plugin", pluginId);
    } else {
      nextParams.delete("plugin");
    }

    if (target) {
      nextParams.set("target", target);
    } else {
      nextParams.delete("target");
    }

    searchParams.value = nextParams;
    writeSearchParams(nextParams);
  }

  function requestedPluginId() {
    return searchParams.value.get("plugin")?.trim() || null;
  }

  function requestedTarget() {
    return searchParams.value.get("target")?.trim() || null;
  }

  return {
    searchParams,
    showExperimental,
    syncSelection,
    requestedPluginId,
    requestedTarget,
  };
}
