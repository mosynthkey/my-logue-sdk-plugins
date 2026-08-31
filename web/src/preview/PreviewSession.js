import { previewDebugLog } from "../composables/usePreviewDebugLog.js";

function runtimeDocumentUrl() {
  const path = window.location.pathname.endsWith("/")
    ? window.location.pathname
    : window.location.pathname.replace(/\/[^/]*$/, "/");
  return `${window.location.origin}${path}preview-runtime.html`;
}

export class PreviewSession {
  constructor() {
    this.iframe = null;
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
    iframe.style.cssText = [
      "position:fixed",
      "left:0",
      "top:0",
      "width:1px",
      "height:1px",
      "opacity:0",
      "border:0",
      "pointer-events:none",
    ].join(";");

    const loaded = new Promise((resolve, reject) => {
      iframe.addEventListener("load", resolve, { once: true });
      iframe.addEventListener("error", () => {
        reject(new Error("Failed to load preview runtime"));
      }, { once: true });
    });

    iframe.src = runtimeDocumentUrl();
    document.body.append(iframe);
    this.iframe = iframe;
    await loaded;

    const frameWindow = iframe.contentWindow;
    if (!frameWindow?.__previewHost) {
      throw new Error("Preview runtime failed to initialize");
    }

    frameWindow.addEventListener("error", (event) => {
      previewDebugLog("error", event.message || "Runtime error");
    });
    frameWindow.addEventListener("unhandledrejection", (event) => {
      previewDebugLog("error", event.reason || "Runtime promise rejection");
    });
  }

  async destroy() {
    if (!this.iframe) {
      return;
    }
    this.iframe.remove();
    this.iframe = null;
  }
}
