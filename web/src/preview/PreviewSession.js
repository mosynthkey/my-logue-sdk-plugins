import { previewDebugLog } from "../composables/usePreviewDebugLog.js";

const HIDDEN_FRAME_STYLE = [
  "position:fixed",
  "left:0",
  "top:0",
  "width:1px",
  "height:1px",
  "opacity:0",
  "border:0",
  "pointer-events:none",
  "z-index:-1",
].join(";");

function assetUrl(relativePath) {
  return new URL(relativePath, window.location.href).href;
}

function runtimeBootstrapHtml() {
  const runtimeUrl = assetUrl("preview-runtime.js");
  // Do not register coi-serviceworker here — the parent page owns COI and nested
  // registration breaks iOS Safari (see edbb59d).
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Plugin preview runtime</title>
</head>
<body>
  <script src="${runtimeUrl}"><\/script>
</body>
</html>`;
}

async function waitForPreviewHost(frameWindow, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (frameWindow?.__previewHost) {
      return;
    }
    await new Promise((resolve) => {
      window.setTimeout(resolve, 50);
    });
  }
  throw new Error("Preview runtime failed to initialize");
}

export class PreviewSession {
  constructor() {
    this.iframe = null;
    this.gestureCaptureTarget = null;
  }

  get host() {
    return this.iframe?.contentWindow?.__previewHost ?? null;
  }

  async attach() {
    await this.destroy();
    window.__previewRuntimeLog = previewDebugLog;

    const iframe = document.createElement("iframe");
    iframe.className = "preview-runtime-frame";
    iframe.setAttribute("aria-hidden", "true");
    iframe.setAttribute("allow", "autoplay");
    iframe.tabIndex = -1;
    iframe.style.cssText = HIDDEN_FRAME_STYLE;

    const loaded = new Promise((resolve, reject) => {
      iframe.addEventListener("load", resolve, { once: true });
      iframe.addEventListener("error", () => {
        reject(new Error("Failed to load preview runtime"));
      }, { once: true });
    });

    iframe.src = "about:blank";
    document.body.append(iframe);
    this.iframe = iframe;
    await loaded;

    const frameDocument = iframe.contentDocument;
    if (!frameDocument) {
      throw new Error("Preview runtime iframe is inaccessible");
    }

    frameDocument.open();
    frameDocument.write(runtimeBootstrapHtml());
    frameDocument.close();

    const frameWindow = iframe.contentWindow;
    await waitForPreviewHost(frameWindow);

    frameWindow.addEventListener("error", (event) => {
      previewDebugLog("error", event.message || "Runtime error");
    });
    frameWindow.addEventListener("unhandledrejection", (event) => {
      previewDebugLog("error", event.reason || "Runtime promise rejection");
    });
  }

  setGestureCapture(enabled, captureTarget = null) {
    if (!this.iframe) {
      return;
    }

    this.gestureCaptureTarget = enabled ? captureTarget : null;
    if (!enabled || !captureTarget) {
      this.iframe.style.cssText = HIDDEN_FRAME_STYLE;
      this.host?.disarmGestureStart?.();
      return;
    }

    const bounds = captureTarget.getBoundingClientRect();
    this.iframe.style.cssText = [
      "position:fixed",
      `left:${Math.max(0, bounds.left)}px`,
      `top:${Math.max(0, bounds.top)}px`,
      `width:${Math.max(1, bounds.width)}px`,
      `height:${Math.max(1, bounds.height)}px`,
      "opacity:0",
      "border:0",
      "pointer-events:auto",
      "z-index:1000",
      "background:transparent",
    ].join(";");

    this.host?.armGestureStart?.(() => {
      this.setGestureCapture(false);
      window.__previewGestureDone?.();
    });
  }

  async destroy() {
    this.setGestureCapture(false);
    if (!this.iframe) {
      return;
    }
    this.iframe.remove();
    this.iframe = null;
  }
}
