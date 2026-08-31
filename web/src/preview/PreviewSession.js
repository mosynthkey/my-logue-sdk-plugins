import { previewDebugLog } from "../composables/usePreviewDebugLog.js";

function assetUrl(relativePath) {
  return new URL(relativePath, window.location.href).href;
}

function runtimeBootstrapHtml() {
  const coiUrl = assetUrl("coi-serviceworker.js");
  const runtimeUrl = assetUrl("preview-runtime.js");
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Plugin preview runtime</title>
  <script>
    window.coi = {
      coepCredentialless: () => true,
      coepDegrade: () => false,
      quiet: true,
      ...(window.coi || {}),
    };
  <\/script>
  <script src="${coiUrl}"><\/script>
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

  async destroy() {
    if (!this.iframe) {
      return;
    }
    this.iframe.remove();
    this.iframe = null;
  }
}
