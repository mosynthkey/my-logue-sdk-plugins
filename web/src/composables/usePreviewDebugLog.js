import { computed, ref } from "vue";

const MAX_LOG_LINES = 80;
const lines = ref([]);
const enabled = ref(false);

function detectDebugEnabled() {
  if (typeof window === "undefined") {
    return false;
  }
  const params = new URLSearchParams(window.location.search);
  return params.has("previewDebug") || params.has("debug");
}

function formatDetail(detail) {
  if (detail === undefined || detail === null) {
    return "";
  }
  if (detail instanceof Error) {
    return detail.stack || detail.message;
  }
  if (typeof detail === "string") {
    return detail;
  }
  try {
    return JSON.stringify(detail);
  } catch {
    return String(detail);
  }
}

export function initPreviewDebugLog() {
  if (enabled.value || typeof window === "undefined") {
    return;
  }
  enabled.value = detectDebugEnabled();

  const originalConsoleError = console.error.bind(console);
  console.error = (...args) => {
    previewDebugLog("error", args.map((arg) => formatDetail(arg)).join(" "));
    originalConsoleError(...args);
  };

  window.addEventListener("error", (event) => {
    previewDebugLog("error", event.message || "Unhandled error");
  });

  window.addEventListener("unhandledrejection", (event) => {
    previewDebugLog("error", formatDetail(event.reason) || "Unhandled promise rejection");
  });
}

export function previewDebugLog(kind, message, detail) {
  const detailText = formatDetail(detail);
  const text = detailText ? `${message} — ${detailText}` : message;
  lines.value = [
    {
      kind,
      message: text,
      at: new Date().toISOString(),
    },
    ...lines.value,
  ].slice(0, MAX_LOG_LINES);

  if (kind === "error") {
    enabled.value = true;
  }
}

export function usePreviewDebugLog() {
  initPreviewDebugLog();

  const visible = computed(() => enabled.value && lines.value.length > 0);

  function copyLog() {
    const payload = lines.value
      .slice()
      .reverse()
      .map((line) => `${line.at} [${line.kind}] ${line.message}`)
      .join("\n");
    return navigator.clipboard?.writeText(payload);
  }

  return {
    lines,
    visible,
    copyLog,
    previewDebugLog,
  };
}
