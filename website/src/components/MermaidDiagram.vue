<script setup>
import { nextTick, onBeforeUnmount, ref, watch } from "vue";

const props = defineProps({
  chart: {
    type: String,
    required: true,
  },
  diagramId: {
    type: String,
    required: true,
  },
});

const hostRef = ref(null);
const renderError = ref(null);
let renderToken = 0;
let mermaidReady = null;

async function ensureMermaid() {
  if (!mermaidReady) {
    mermaidReady = import("mermaid").then((module) => {
      const mermaid = module.default;
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "dark",
        fontFamily: "IBM Plex Mono, monospace",
        flowchart: {
          curve: "basis",
          htmlLabels: false,
          padding: 12,
        },
        themeVariables: {
          darkMode: true,
          background: "#0a0a0a",
          primaryColor: "#141414",
          primaryTextColor: "#f0f0f0",
          primaryBorderColor: "#b6b2a1",
          lineColor: "#888888",
          secondaryColor: "#1a1a1a",
          tertiaryColor: "#0a0a0a",
          fontFamily: "IBM Plex Mono, monospace",
        },
      });
      return mermaid;
    });
  }
  return mermaidReady;
}

async function renderDiagram() {
  const token = ++renderToken;
  renderError.value = null;
  await nextTick();
  if (!hostRef.value || !props.chart.trim()) return;

  try {
    const mermaid = await ensureMermaid();
    if (token !== renderToken || !hostRef.value) return;

    const { svg } = await mermaid.render(
      `mermaid-${props.diagramId}-${token}`,
      props.chart.trim(),
    );
    if (token !== renderToken || !hostRef.value) return;
    hostRef.value.innerHTML = svg;
    const svgElement = hostRef.value.querySelector("svg");
    if (svgElement) {
      svgElement.removeAttribute("height");
      svgElement.style.width = "100%";
      svgElement.style.height = "auto";
      svgElement.style.maxWidth = "100%";
    }
  } catch (error) {
    if (token !== renderToken) return;
    renderError.value = error?.message || "Failed to render diagram";
    if (hostRef.value) hostRef.value.innerHTML = "";
  }
}

watch(
  () => [props.chart, props.diagramId],
  () => {
    renderDiagram();
  },
  { immediate: true },
);

onBeforeUnmount(() => {
  renderToken += 1;
});
</script>

<template>
  <div class="mermaid-wrap">
    <div
      ref="hostRef"
      class="mermaid-host"
      aria-hidden="true"
    />
    <p
      v-if="renderError"
      class="mermaid-error"
    >
      {{ renderError }}
    </p>
  </div>
</template>
